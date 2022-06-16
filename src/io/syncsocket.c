#include "moar.h"
#include "platform/socket.h"

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

/* Assumed maximum packet size. If ever changing this to something beyond a
 * 16-bit number, then make sure to change the receive offsets in the data
 * structure below. */
#define PACKET_SIZE 65535

/* Error handling varies between POSIX and WinSock. */
MVM_NO_RETURN static void throw_error(MVMThreadContext *tc, int r, char *operation) MVM_NO_RETURN_ATTRIBUTE;
#ifdef _WIN32
    #define MVM_IS_INVALID_SOCKET(x) ((x) == INVALID_SOCKET)
    #define MVM_IS_SOCKET_ERROR(x)   ((x) == SOCKET_ERROR)

    static void throw_error(MVMThreadContext *tc, int r, char *operation) {
        int error = WSAGetLastError();
        LPTSTR error_string = NULL;
        if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                NULL, error, 0, (LPTSTR)&error_string, 0, NULL) == 0) {
            /* Couldn't get error string; throw with code. */
            MVM_exception_throw_adhoc(tc, "Could not %s: error code %d", operation, error);
        }
        MVM_exception_throw_adhoc(tc, "Could not %s: %s", operation, error_string);
    }
#else
    #define MVM_IS_INVALID_SOCKET(x) ((x) == -1)
    #define MVM_IS_SOCKET_ERROR(x) ((x) < 0)
    static void throw_error(MVMThreadContext *tc, int r, char *operation) {
        MVM_exception_throw_adhoc(tc, "Could not %s: %s", operation, strerror(errno));
    }
#endif

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* The socket handle (file descriptor on POSIX, SOCKET on Windows). */
    MVMSocket handle;

    /* Buffer of the last received packet of data, and start/end pointers
     * into the data. */
    char *last_packet;
    MVMuint16 last_packet_start;
    MVMuint16 last_packet_end;

    /* Did we reach EOF yet? */
    MVMint32 eof;

    /* ID for instrumentation. */
    unsigned int interval_id;
} MVMIOSyncSocketData;

/* Read a packet worth of data into the last packet buffer. */
static void read_one_packet(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    unsigned int interval_id = MVM_telemetry_interval_start(tc, "syncsocket.read_one_packet");
    int r;
    data->last_packet = MVM_malloc(PACKET_SIZE);
    do {
        MVM_gc_mark_thread_blocked(tc);
        r = recv(data->handle, data->last_packet, PACKET_SIZE, 0);
        MVM_gc_mark_thread_unblocked(tc);
    } while(MVM_IS_SOCKET_ERROR(r) && errno == EINTR);
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.read_one_packet");
    if (MVM_IS_SOCKET_ERROR(r) || r == 0) {
        MVM_free_null(data->last_packet);
        if (r != 0)
            throw_error(tc, r, "receive data from socket");
    }
    else {
        data->last_packet_start = 0;
        data->last_packet_end = r;
    }
}

static MVMint64 socket_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMuint64 bytes) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    char *use_last_packet = NULL;
    MVMuint16 use_last_packet_start = 0, use_last_packet_end = 0;

    /* If at EOF, nothing more to do. */
    if (data->eof) {
        *buf = NULL;
        return 0;
    }

    /* See if there's anything in the packet buffer. */
    if (data->last_packet) {
        MVMuint16 last_remaining = data->last_packet_end - data->last_packet_start;
        if (bytes <= last_remaining) {
            /* There's enough, and it's sufficient for the request. Extract it
             * and return, discarding the last packet buffer if we drain it. */
            *buf = MVM_malloc(bytes);
            memcpy(*buf, data->last_packet + data->last_packet_start, bytes);
            if (bytes == last_remaining) {
                MVM_free_null(data->last_packet);
            }
            else {
                data->last_packet_start += bytes;
            }
            return bytes;
        }
        else {
            /* Something, but not enough. Take the last packet for use, then
             * we'll read another one. */
            use_last_packet = data->last_packet;
            use_last_packet_start = data->last_packet_start;
            use_last_packet_end = data->last_packet_end;
            data->last_packet = NULL;
        }
    }

    /* If we get here, we need to read another packet. */
    read_one_packet(tc, data);

    /* Now assemble the result. */
    if (data->last_packet && use_last_packet) {
        /* Need to assemble it from two places. */
        MVMuint32 last_available = use_last_packet_end - use_last_packet_start;
        MVMuint32 available = last_available + data->last_packet_end;
        if (bytes > available)
            bytes = available;
        *buf = MVM_malloc(bytes);
        memcpy(*buf, use_last_packet + use_last_packet_start, last_available);
        memcpy(*buf + last_available, data->last_packet, bytes - last_available);
        if (bytes == available) {
            /* We used all of the just-read packet. */
            MVM_free_null(data->last_packet);
        }
        else {
            /* Still something left in the just-read packet for next time. */
            data->last_packet_start += bytes - last_available;
        }
        MVM_free(use_last_packet);
    }
    else if (data->last_packet) {
        /* Only data from the just-read packet. */
        if (bytes >= data->last_packet_end) {
            /* We need all of it, so no copying needed, just hand it back. */
            *buf = data->last_packet;
            bytes = data->last_packet_end;
            data->last_packet = NULL;
        }
        else {
            /* Only need some of it. */
            *buf = MVM_malloc(bytes);
            memcpy(*buf, data->last_packet, bytes);
            data->last_packet_start += bytes;
        }
    }
    else if (use_last_packet) {
        /* Nothing read this time, so at the end. Drain previous packet data
         * and mark EOF. */
        bytes = use_last_packet_end - use_last_packet_start;
        *buf = MVM_malloc(bytes);
        memcpy(*buf, use_last_packet + use_last_packet_start, bytes);
        data->eof = 1;
        MVM_free(use_last_packet);
    }
    else {
        /* Nothing to hand back; at EOF. */
        *buf = NULL;
        bytes = 0;
        data->eof = 1;
    }

    return bytes;
}

/* Checks if EOF has been reached on the incoming data. */
static MVMint64 socket_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    return data->eof;
}

static void socket_flush(MVMThreadContext *tc, MVMOSHandle *h, MVMint32 sync) {
    /* A no-op for sockets; we don't buffer. */
}

static void socket_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVM_exception_throw_adhoc(tc, "Cannot truncate a socket");
}

/* Writes the specified bytes to the stream. */
static MVMint64 socket_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMuint64 bytes) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    MVMint64 sent = 0;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket.write_bytes");
    MVM_gc_mark_thread_blocked(tc);
    while (bytes > 0) {
        int r;
        do {
            r = send(data->handle, buf, bytes > 0x40000000 ? 0x40000000 : bytes, 0);
        } while(MVM_IS_SOCKET_ERROR(r) && errno == EINTR);
        if (MVM_IS_SOCKET_ERROR(r)) {
            MVM_gc_mark_thread_unblocked(tc);
            MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.write_bytes");
            throw_error(tc, r, "send data to socket");
        }
        sent += r;
        buf += r;
        bytes -= r;
    }
    MVM_gc_mark_thread_unblocked(tc);
    MVM_telemetry_interval_annotate(bytes, interval_id, "written this many bytes");
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.write_bytes");
    return bytes;
}

static MVMint64 do_close(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    if (data->handle) {
        MVM_platform_close_socket(data->handle);
        data->handle = 0;
    }
    return 0;
}
static MVMint64 close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    return do_close(tc, (MVMIOSyncSocketData *)h->body.data);
}

static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)d;
    do_close(tc, data);
    MVM_free(data);
}

static size_t get_struct_size_for_family(sa_family_t family) {
    switch (family) {
        case AF_INET6:
            return sizeof(struct sockaddr_in6);
        case AF_INET:
            return sizeof(struct sockaddr_in);
#ifdef MVM_HAS_PF_UNIX
        case AF_UNIX:
            return sizeof(struct sockaddr_un);
#endif
        default:
            return sizeof(struct sockaddr);
    }
}

/*
 * This function resolves a hostname given a port and family (i.e. domain),
 * returning a valid address to use with a socket under the given domain,
 * type, and protocol, in addition to whether or not the socket is passive.
 *
 * Valid families:
 * - SOCKET_FAMILY_INET   (PF_INET)
 * - SOCKET_FAMILY_INET6  (PF_INET6)
 * - SOCKET_FAMILY_UNIX   (PF_UNIX)
 * - SOCKET_FAMILY_UNSPEC (PF_UNSPEC)
 *
 * Valid types:
 * - SOCKET_TYPE_STREAM    (SOCK_STREAM)
 * - SOCKET_TYPE_DGRAM     (SOCK_DGRAM)
 * - SOCKET_TYPE_RAW       (SOCK_RAW)
 * - SOCKET_TYPE_RDM       (SOCK_RDM)
 * - SOCKET_TYPE_SEQPACKET (SOCK_SEQPACKET)
 * - SOCKET_TYPE_ANY       (any acceptable type)
 *
 * Valid protocols:
 * - SOCKET_PROTOCOL_TCP (IPPROTO_TCP)
 * - SOCKET_PROTOCOL_UDP (IPPROTO_UDP)
 * - SOCKET_PROTOCOL_ANY (any acceptable protocol)
 */

struct sockaddr * MVM_io_resolve_host_name(MVMThreadContext *tc,
        MVMString *host, MVMint64 port,
        MVMuint16 family, MVMint64 type, MVMint64 protocol,
        MVMint32 passive) {
    char *host_cstr     = MVM_string_utf8_encode_C_string(tc, host);
    char  port_cstr[8];

    struct addrinfo  hints;
    struct addrinfo *result;
    struct sockaddr *address;
    size_t           address_len;

    int error;

    memset(&hints, 0, sizeof(hints));
    /* XXX: This shouldn't be treating addresses meant for active sockets like
     *      those for passive ones. */
    hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
    /* if (passive) hints.ai_flags |= AI_PASSIVE; */

    switch (family) {
        case MVM_SOCKET_FAMILY_UNSPEC:
            hints.ai_family = AF_UNSPEC;
            break;
        case MVM_SOCKET_FAMILY_INET:
            hints.ai_family = AF_INET;
            break;
        case MVM_SOCKET_FAMILY_INET6:
            hints.ai_family = AF_INET6;
            break;
        case MVM_SOCKET_FAMILY_UNIX: {
#ifdef MVM_HAS_PF_UNIX
            size_t sun_len = strnlen(host_cstr, MVM_SUN_PATH_SIZE);

            if (sun_len >= MVM_SUN_PATH_SIZE) {
                char *waste[] = { host_cstr, NULL };
                MVM_exception_throw_adhoc_free(
                    tc, waste, "Socket path '%s' is too long (max length supported by this platform is %zu characters)",
                    host_cstr, MVM_SUN_PATH_SIZE - 1
                );
            } else {
                struct sockaddr_un *result_un = MVM_malloc(sizeof(struct sockaddr_un));
                result_un->sun_family         = AF_UNIX;
                strcpy(result_un->sun_path, host_cstr);
                MVM_free(host_cstr);
                return (struct sockaddr *)result_un;
            }
#else
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "UNIX sockets are not supported by MoarVM on this platform");
#endif
        }
        default:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Unsupported socket family: %"PRIu16"", family);
            break;
    }

    switch (type) {
        case MVM_SOCKET_TYPE_ANY:
            hints.ai_socktype = 0;
            break;
        case MVM_SOCKET_TYPE_STREAM:
            hints.ai_socktype = SOCK_STREAM;
            break;
        case MVM_SOCKET_TYPE_DGRAM:
            hints.ai_socktype = SOCK_DGRAM;
            break;
        case MVM_SOCKET_TYPE_RAW:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Support for raw sockets NYI");
        case MVM_SOCKET_TYPE_RDM:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Support for RDM sockets NYI");
        case MVM_SOCKET_TYPE_SEQPACKET:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Support for seqpacket sockets NYI");
        default:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Unsupported socket type: %"PRIi64"", type);
    }

    switch (protocol) {
        case MVM_SOCKET_PROTOCOL_ANY:
            hints.ai_protocol = 0;
            break;
        case MVM_SOCKET_PROTOCOL_TCP:
            hints.ai_protocol = IPPROTO_TCP;
            break;
        case MVM_SOCKET_PROTOCOL_UDP:
            hints.ai_protocol = IPPROTO_UDP;
            break;
        default:
            MVM_free(host_cstr);
            MVM_exception_throw_adhoc(tc, "Unsupported socket protocol: %"PRIi64"", protocol);
    }

    snprintf(port_cstr, 8, "%d", (int)port);

    MVM_gc_mark_thread_blocked(tc);
    error = getaddrinfo(host_cstr, port_cstr, &hints, &result);
    MVM_gc_mark_thread_unblocked(tc);

    if (error != 0) {
        char *waste[] = { host_cstr, NULL };
        MVM_exception_throw_adhoc_free(
            tc, waste, "Failed to resolve host name '%s' with family %"PRIu16".\nError: %s",
            host_cstr, family, gai_strerror(error)
        );
    }

    MVM_free(host_cstr);

    address_len = get_struct_size_for_family(result->ai_family);
    address     = MVM_malloc(address_len);
    memcpy(address, result->ai_addr, address_len);
    freeaddrinfo(result);
    return address;
}

/* Establishes a connection. */
static void socket_connect(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket connect");
    if (!data->handle) {
        struct sockaddr *dest = MVM_io_resolve_host_name(tc, host, port, family, MVM_SOCKET_TYPE_STREAM, MVM_SOCKET_PROTOCOL_ANY, 0);
        int r;

        MVMSocket s = data->handle = socket(dest->sa_family , SOCK_STREAM , 0);
        if (MVM_IS_INVALID_SOCKET(s)) {
            MVM_free(dest);
            MVM_telemetry_interval_stop(tc, interval_id, "syncsocket connect");
            throw_error(tc, s, "create socket");
        }

        do {
            MVM_gc_mark_thread_blocked(tc);
            r = connect(s, dest, (socklen_t)get_struct_size_for_family(dest->sa_family));
            MVM_gc_mark_thread_unblocked(tc);
        } while(MVM_IS_SOCKET_ERROR(r) && errno == EINTR);
        MVM_free(dest);
        if (MVM_IS_SOCKET_ERROR(r)) {
            MVM_telemetry_interval_stop(tc, interval_id, "syncsocket connect");
            throw_error(tc, s, "connect to socket");
        }
    }
    else {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket didn't connect");
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }
}

static void socket_bind(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family, MVMint32 backlog) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    if (!data->handle) {
        struct sockaddr *dest = MVM_io_resolve_host_name(tc, host, port, family, MVM_SOCKET_TYPE_STREAM, MVM_SOCKET_PROTOCOL_ANY, 1);
        int r;

        MVMSocket s = data->handle = socket(dest->sa_family , SOCK_STREAM , 0);
        if (MVM_IS_INVALID_SOCKET(s)) {
            MVM_free(dest);
            throw_error(tc, s, "create socket");
        }

        /* On POSIX, we set the SO_REUSEADDR option, which allows re-use of
         * a port in TIME_WAIT state (modulo many hair details). Oringinally,
         * MoarVM used libuv, which does this automatically on non-Windows.
         * We have tests with bring up a server, then take it down, and then
         * bring another up on the same port, and we get test failures due
         * to racing to re-use the port without this. */
#ifndef _WIN32
        {
            int one = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        }
#endif

        r = bind(s, dest, (socklen_t)get_struct_size_for_family(dest->sa_family));
        MVM_free(dest);
        if (MVM_IS_SOCKET_ERROR(r))
            throw_error(tc, s, "bind socket");

        r = listen(s, (int)backlog);
        if (MVM_IS_SOCKET_ERROR(r))
            throw_error(tc, s, "start listening on socket");
    }
    else {
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }
}

static MVMint64 socket_getport(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;

    struct sockaddr_storage name;
    int error;
    socklen_t len = sizeof(struct sockaddr_storage);
    MVMint64 port = 0;

    error = getsockname(data->handle, (struct sockaddr *) &name, &len);

    if (error != 0)
        MVM_exception_throw_adhoc(tc, "Failed to getsockname: %s", strerror(errno));

    switch (name.ss_family) {
        case AF_INET6:
            port = ntohs((*( struct sockaddr_in6 *) &name).sin6_port);
            break;
        case AF_INET:
            port = ntohs((*( struct sockaddr_in *) &name).sin_port);
            break;
    }

    return port;
}

static MVMint64 socket_is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    return (MVMint64)MVM_platform_isatty(data->handle);
}

static MVMint64 socket_handle(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    return (MVMint64)data->handle;
}

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h);

/* IO ops table, populated with functions. */
static const MVMIOClosable      closable      = { close_socket };
static const MVMIOSyncReadable  sync_readable = { socket_read_bytes,
                                                  socket_eof };
static const MVMIOSyncWritable  sync_writable = { socket_write_bytes,
                                                  socket_flush,
                                                  socket_truncate };
static const MVMIOSockety             sockety = { socket_connect,
                                                  socket_bind,
                                                  socket_accept,
                                                  socket_getport };
static const MVMIOIntrospection introspection = { socket_is_tty,
                                                  socket_handle };

static const MVMIOOps op_table = {
    &closable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    NULL,
    &sockety,
    NULL,
    NULL,
    &introspection,
    NULL,
    NULL,
    gc_free
};

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    MVMSocket            s;

    unsigned int interval_id = MVM_telemetry_interval_start(tc, "syncsocket accept");
    do {
        MVM_gc_mark_thread_blocked(tc);
        s = accept(data->handle, NULL, NULL);
        MVM_gc_mark_thread_unblocked(tc);
    } while(MVM_IS_INVALID_SOCKET(s) && errno == EINTR);
    if (MVM_IS_INVALID_SOCKET(s)) {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept failed");
        throw_error(tc, s, "accept socket connection");
    }
    else {
        MVMOSHandle * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTIO);
        MVMIOSyncSocketData * const data = MVM_calloc(1, sizeof(MVMIOSyncSocketData));
        data->handle = s;
        result->body.ops  = &op_table;
        result->body.data = data;
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept succeeded");
        return (MVMObject *)result;
    }
}

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen) {
    MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncSocketData * const data   = MVM_calloc(1, sizeof(MVMIOSyncSocketData));
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}

MVMString * MVM_io_get_hostname(MVMThreadContext *tc) {
    char hostname[UV_MAXHOSTNAMESIZE];
    size_t size = UV_MAXHOSTNAMESIZE;
    int result = uv_os_gethostname(hostname, &size);

    if(result < 0) {
        MVM_exception_throw_adhoc(tc, "Failed to get hostname: %i", result);
    }

    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, hostname);
}
