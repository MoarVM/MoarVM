#include "moar.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>

    typedef SOCKET Socket;
    #define sa_family_t unsigned int
    #define isatty _isatty
#else
    #include "unistd.h"
    #include <sys/socket.h>
    #include <sys/un.h>

    typedef int Socket;
    #define closesocket close
#endif

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

/* Assumed maximum packet size. If ever changing this to something beyond a
 * 16-bit number, then make sure to change the receive offsets in the data
 * structure below. */
#define PACKET_SIZE 65535

/* Error handling varies between POSIX and WinSock. */
MVM_NO_RETURN static void throw_error(MVMThreadContext *tc, int r, const char *operation) MVM_NO_RETURN_ATTRIBUTE;
#ifdef _WIN32
    #define MVM_IS_SOCKET_ERROR(x) ((x) == SOCKET_ERROR)
    static void throw_error(MVMThreadContext *tc, int r, const char *operation) {
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
    #define MVM_IS_SOCKET_ERROR(x) ((x) < 0)
    static void throw_error(MVMThreadContext *tc, int r, const char *operation) {
        MVM_exception_throw_adhoc(tc, "Could not %s: %s", operation, strerror(errno));
    }
#endif

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* The socket handle (file descriptor on POSIX, SOCKET on Windows). */
    Socket handle;

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
    } while(r == -1 && errno == EINTR);
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.read_one_packet");
    if (MVM_IS_SOCKET_ERROR(r) || r == 0) {
        MVM_free(data->last_packet);
        data->last_packet = NULL;
        if (r != 0)
            throw_error(tc, r, "receive data from socket");
    }
    else {
        data->last_packet_start = 0;
        data->last_packet_end = r;
    }
}

MVMint64 socket_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    char *use_last_packet = NULL;
    MVMuint16 use_last_packet_start, use_last_packet_end;

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
                MVM_free(data->last_packet);
                data->last_packet = NULL;
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
            MVM_free(data->last_packet);
            data->last_packet = NULL;
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
MVMint64 socket_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    return data->eof;
}

void socket_flush(MVMThreadContext *tc, MVMOSHandle *h, MVMint32 sync) {
    /* A no-op for sockets; we don't buffer. */
}

void socket_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVM_exception_throw_adhoc(tc, "Cannot truncate a socket");
}

/* Writes the specified bytes to the stream. */
MVMint64 socket_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    MVMint64 sent = 0;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket.write_bytes");
    MVM_gc_mark_thread_blocked(tc);
    while (bytes > 0) {
        int r;
        do {
            r = send(data->handle, buf, (int)bytes, 0);
        } while(r == -1 && errno == EINTR);
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
        closesocket(data->handle);
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
#ifndef _WIN32
        case AF_UNIX:
            return sizeof(struct sockaddr_un);
#endif
        default:
            return sizeof(struct sockaddr);
    }
}

/*
 * This function may return any type of sockaddr e.g. sockaddr_un, sockaddr_in or sockaddr_in6
 * It shouldn't be a problem with general code as long as the port number is kept below the int16 limit: 65536
 * After this it defines the family which may spawn non internet sockaddr's
 * The family can be extracted by (port >> 16) & USHORT_MAX
 *
 * Currently supported families:
 *
 * SOCKET_FAMILY_UNSPEC = 0
 *   Unspecified, in most cases should be equal to AF_INET or AF_INET6
 *
 * SOCKET_FAMILY_INET = 1
 *   IPv4 socket
 *
 * SOCKET_FAMILY_INET6 = 2
 *   IPv6 socket
 *
 * SOCKET_FAMILY_UNIX = 3
 *   Unix domain socket, will spawn a sockaddr_un which will use the given host as path
 *   e.g: MVM_io_resolve_host_name(tc, "/run/moarvm.sock", 0, SOCKET_FAMILY_UNIX, 0, 0, 0)
 *   will spawn an unix domain socket on /run/moarvm.sock
 *
 * TODO: proper support for the types and protocols that can be passed to this.
 *
 * Passing passive as 1 will look up hostnames for sockets intended for
 * bind(2), and passing it as 0 will look them up for those intended for
 * connect(2).
 */

struct addrinfo * MVM_io_resolve_host_name(
    MVMThreadContext *tc,
    MVMString        *host,
    MVMint64          port,
    MVMuint16         family,
    MVMint64          type,
    MVMint64          protocol,
    MVMint64          passive
) {
    char            *host_cstr     = MVM_string_utf8_encode_C_string(tc, host);
    char             port_cstr[8];
    int              error;
    struct addrinfo  hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = passive
                   ? (AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE)
                   : (AI_ADDRCONFIG | AI_NUMERICSERV);

    switch (family) {
        case SOCKET_FAMILY_UNSPEC:
            hints.ai_family = AF_UNSPEC;
            break;
        case SOCKET_FAMILY_INET:
            hints.ai_family = AF_INET;
            break;
        case SOCKET_FAMILY_INET6:
            hints.ai_family = AF_INET6;
            break;
        case SOCKET_FAMILY_UNIX: {
            /* XXX: this is pretty hacky... */
#ifdef _WIN32
            MVM_exception_throw_adhoc(tc, "UNIX sockets are unsupported on Windows.");
#else
            struct sockaddr_un *dest_un;
            MVMuint64           host_len = strlen(host_cstr);

            if (host_len > 107) {
                char *waste[] = { host_cstr, NULL };
                MVM_free(host_cstr);
                MVM_exception_throw_adhoc_free(tc, waste, "Socket path '%s' is %"PRIu64" characters, max allowed is 107", host_cstr, host_len);
            }

            dest_un             = MVM_malloc(sizeof(struct sockaddr_un));
            dest_un->sun_family = AF_UNIX;
            strcpy(dest_un->sun_path, host_cstr);
            MVM_free(host_cstr);

            result             = MVM_calloc(1, sizeof(struct addrinfo));
            result->ai_family  = AF_UNIX;
            result->ai_addrlen = sizeof(struct sockaddr_un);
            result->ai_addr    = (struct sockaddr *)dest_un;
            return result;
#endif
        }
        default:
            MVM_exception_throw_adhoc(tc, "Unsupported socket family: %hu", family);
    }

    switch (type) {
        case 0:
            hints.ai_socktype = 0;
            break;
        case SOCKET_TYPE_STREAM:
            hints.ai_socktype = SOCK_STREAM;
            break;
        case SOCKET_TYPE_DGRAM:
            hints.ai_socktype = SOCK_DGRAM;
            break;
        case SOCKET_TYPE_RAW:
            hints.ai_socktype = SOCK_RAW;
            break;
        case SOCKET_TYPE_RDM:
            hints.ai_socktype = SOCK_RDM;
            break;
        case SOCKET_TYPE_SEQPACKET:
            hints.ai_socktype = SOCK_SEQPACKET;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unsupported socket type: %ld", type);
    }

    switch (protocol) {
        case 0:
            hints.ai_protocol = 0;
            break;
        case SOCKET_PROTOCOL_TCP:
            hints.ai_protocol = IPPROTO_TCP;
            break;
        case SOCKET_PROTOCOL_UDP:
            hints.ai_protocol = IPPROTO_UDP;
            break;
        case SOCKET_PROTOCOL_RAW:
            hints.ai_protocol = IPPROTO_RAW;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Unsupported socket protocol: %ld", protocol);
    }

    snprintf(port_cstr, 8, "%d", (int)port);

    MVM_gc_mark_thread_blocked(tc);
    error = getaddrinfo(host_cstr, port_cstr, &hints, &result);
    MVM_gc_mark_thread_unblocked(tc);

    if (error) {
        char *waste[] = { host_cstr, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Failed to resolve host name '%s' with family %hu. Error: '%s'",
                                       host_cstr, family, gai_strerror(error));
    }

    return result;
}

/* Establishes a connection. */
static void socket_connect(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family) {
    MVMIOSyncSocketData *data        = (MVMIOSyncSocketData *)h->body.data;
    unsigned int         interval_id = MVM_telemetry_interval_start(tc, "syncsocket connect");

    struct addrinfo *address_info;
    Socket           s;
    int              r;
    const char      *errmsg;

    if (data->handle) {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket didn't connect");
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }

    address_info = MVM_io_resolve_host_name(tc, host, port, family, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP, 0);

    MVM_gc_mark_thread_blocked(tc);
    s = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
    MVM_gc_mark_thread_unblocked(tc);
    if (MVM_IS_SOCKET_ERROR(s)) {
        errmsg = "create socket";
        goto error;
    }

    do {
        MVM_gc_mark_thread_blocked(tc);
        r = connect(s, address_info->ai_addr, address_info->ai_addrlen);
        MVM_gc_mark_thread_unblocked(tc);
    } while (r == -1 && errno == EINTR);

    if (MVM_IS_SOCKET_ERROR(r)) {
        int saved_errno;

        MVM_gc_mark_thread_blocked(tc);
        saved_errno = errno;
        close(s);
        errno = saved_errno;
        MVM_gc_mark_thread_unblocked(tc);

        errmsg = "connect socket";
        goto error;
    }

    data->handle = s;
    return;

error:
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket connect");
    throw_error(tc, s, errmsg);
}

static void socket_bind(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family, MVMint32 backlog) {
    MVMIOSyncSocketData *data        = (MVMIOSyncSocketData *)h->body.data;
    unsigned int         interval_id = MVM_telemetry_interval_start(tc, "syncsocket listen");

    struct addrinfo *address_info = MVM_io_resolve_host_name(tc, host, port, family, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP, 1);
    Socket           s;
    int              r;
    const char      *errmsg       = NULL;

    if (data->handle) {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket didn't listen");
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }

    MVM_gc_mark_thread_blocked(tc);
    s = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
    MVM_gc_mark_thread_unblocked(tc);
    if (MVM_IS_SOCKET_ERROR(s)) {
        errmsg = "create socket";
        goto error;
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

        MVM_gc_mark_thread_blocked(tc);
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        MVM_gc_mark_thread_unblocked(tc);
    }
#endif

    MVM_gc_mark_thread_blocked(tc);
    r = bind(s, address_info->ai_addr, address_info->ai_addrlen);
    MVM_gc_mark_thread_unblocked(tc);
    if (MVM_IS_SOCKET_ERROR(r)) {
        int saved_errno;

        MVM_gc_mark_thread_blocked(tc);
        saved_errno = errno;
        close(s);
        errno = saved_errno;
        MVM_gc_mark_thread_unblocked(tc);

        errmsg = "bind socket";
        goto error;
    }

    MVM_gc_mark_thread_blocked(tc);
    r = listen(s, (int)backlog);
    MVM_gc_mark_thread_unblocked(tc);
    if (MVM_IS_SOCKET_ERROR(r)) {
        int saved_errno;

        MVM_gc_mark_thread_blocked(tc);
        saved_errno = errno;
        close(s);
        errno = saved_errno;
        MVM_gc_mark_thread_unblocked(tc);

        errmsg = "start listening on socket";
        goto error;
    }

    data->handle = s;
    return;

error:
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket listen");
    throw_error(tc, s, errmsg);
}

MVMint64 socket_getport(MVMThreadContext *tc, MVMOSHandle *h) {
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
    return (MVMint64)isatty(data->handle);
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
    MVMIOSyncSocketData *data        = (MVMIOSyncSocketData *)h->body.data;
    unsigned int         interval_id = MVM_telemetry_interval_start(tc, "syncsocket accept");
    Socket               s;

    do {
        MVM_gc_mark_thread_blocked(tc);
        s = accept(data->handle, NULL, NULL);
        MVM_gc_mark_thread_unblocked(tc);
    } while (s == -1 && errno == EINTR);

    if (MVM_IS_SOCKET_ERROR(s)) {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept failed");
        throw_error(tc, s, "accept socket connection");
    }
    else {
        MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
        MVMIOSyncSocketData * const data   = MVM_calloc(1, sizeof(MVMIOSyncSocketData));
        data->handle                       = s;
        result->body.ops                   = &op_table;
        result->body.data                  = data;
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
