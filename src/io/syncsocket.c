#include "moar.h"

#ifndef _WIN32
    #include "unistd.h"
#endif

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

/* Assuemd maximum packet size. If ever changing this to something beyond a
 * 16-bit number, then make sure to change the receive offsets in the data
 * structure below. */
#define PACKET_SIZE 65535

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* The libuv handle to the stream-readable thingy. */
    uv_stream_t *handle;

    /* Buffer of the last received packet of data, and start/end pointers
     * into the data. */
    char *last_packet;
    MVMuint16 last_packet_start;
    MVMuint16 last_packet_end;

    /* Did we reach EOF yet? */
    MVMint32 eof;

    /* Status of the last connect attempt, if any. */
    int connect_status;

    /* Details of next connection to accept; NULL if none. */
    int          accept_status;
    uv_stream_t *accept_server;

    /* ID for instrumentation. */
    unsigned int interval_id;
} MVMIOSyncSocketData;

/* Read a packet worth of data into the last packet buffer. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)handle->data;
    size_t size = suggested_size > PACKET_SIZE
        ? PACKET_SIZE
        : (suggested_size > 0 ? suggested_size : 4);
    buf->base   = MVM_malloc(size);
    buf->len    = size;
    MVM_telemetry_interval_annotate(size, data->interval_id, "alloced this much space");
}
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)handle->data;
    if (nread > 0) {
        data->last_packet = buf->base;
        data->last_packet_start = 0;
        data->last_packet_end = nread;
        MVM_telemetry_interval_annotate(nread, data->interval_id, "read this many bytes");
    }
    else if (nread == UV_EOF) {
        data->last_packet = NULL;
        if (buf->base)
            MVM_free(buf->base);
    }
    else if (nread == 0) {
        /* Read nothing, but not EOF; stay in the event loop. */
        if (buf->base)
            MVM_free(buf->base);
        return;
    }
    uv_read_stop(handle);
    uv_unref((uv_handle_t*)handle);
}
static void read_one_packet(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    unsigned int interval_id = MVM_telemetry_interval_start(tc, "syncsocket.read_one_packet");
    int r;
    data->handle->data = data;
    if ((r = uv_read_start(data->handle, on_alloc, on_read)) < 0)
        MVM_exception_throw_adhoc(tc, "Reading from stream failed: %s",
            uv_strerror(r));
    uv_ref((uv_handle_t *)data->handle);
    if (tc->loop != data->handle->loop) {
        MVM_exception_throw_adhoc(tc, "Tried to read() from an IO handle outside its originating thread");
    }
    MVM_gc_mark_thread_blocked(tc);
    uv_run(tc->loop, UV_RUN_DEFAULT);
    MVM_gc_mark_thread_unblocked(tc);
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.read_to_buffer");
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

void socket_flush(MVMThreadContext *tc, MVMOSHandle *h){
    /* A no-op for sockets; we don't buffer. */
}

void socket_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVM_exception_throw_adhoc(tc, "Cannot truncate a socket");
}

/* Writes the specified bytes to the stream. */
static void write_cb(uv_write_t* req, int status) {
    uv_unref((uv_handle_t *)req->handle);
    MVM_free(req);
}
MVMint64 socket_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    uv_write_t *req = MVM_malloc(sizeof(uv_write_t));
    uv_buf_t write_buf = uv_buf_init(buf, bytes);
    int r;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket.write_bytes");
    uv_ref((uv_handle_t *)data->handle);
    if ((r = uv_write(req, data->handle, &write_buf, 1, write_cb)) < 0) {
        uv_unref((uv_handle_t *)data->handle);
        MVM_free(req);
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.write_bytes failed");
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to stream: %s", uv_strerror(r));
    }
    else {
        MVM_gc_mark_thread_blocked(tc);
        uv_run(tc->loop, UV_RUN_DEFAULT);
        MVM_gc_mark_thread_unblocked(tc);
    }
    MVM_telemetry_interval_annotate(bytes, interval_id, "written this many bytes");
    MVM_telemetry_interval_stop(tc, interval_id, "syncsocket.write_bytes");
    return bytes;
}

static void free_on_close_cb(uv_handle_t *handle) {
    MVM_free(handle);
}
static MVMint64 do_close(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    if (data->handle) {
         uv_close((uv_handle_t *)data->handle, free_on_close_cb);
         data->handle = NULL;
    }
    return 0;
}
static MVMint64 close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    return do_close(tc, data);
}

static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)d;
    do_close(tc, data);
    MVM_free(data);
}

/* Actually, it may return sockaddr_in6 as well; it's not a problem for us, because we just
 * pass is straight to uv, and the first thing it does is it looks at the address family,
 * but it's a thing to remember if someone feels like peeking inside the returned struct. */
struct sockaddr * MVM_io_resolve_host_name(MVMThreadContext *tc, MVMString *host, MVMint64 port) {
    char *host_cstr = MVM_string_utf8_encode_C_string(tc, host);
    struct sockaddr *dest;
    struct addrinfo *result;
    int error;
    char port_cstr[8];
    snprintf(port_cstr, 8, "%d", (int)port);

    error = getaddrinfo(host_cstr, port_cstr, NULL, &result);
    MVM_free(host_cstr);
    if (error == 0) {
        if (result->ai_addr->sa_family == AF_INET6) {
            dest = MVM_malloc(sizeof(struct sockaddr_in6));
            memcpy(dest, result->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            dest = MVM_malloc(sizeof(struct sockaddr));
            memcpy(dest, result->ai_addr, sizeof(struct sockaddr));
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Failed to resolve host name");
    }
    freeaddrinfo(result);

    return dest;
}

static void on_connect(uv_connect_t* req, int status) {
    ((MVMIOSyncSocketData *)req->data)->connect_status = status;
    uv_unref((uv_handle_t *)req->handle);
}
static void socket_connect(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket connect");
    if (!data->handle) {
        struct sockaddr *dest    = MVM_io_resolve_host_name(tc, host, port);
        uv_tcp_t        *socket  = MVM_malloc(sizeof(uv_tcp_t));
        uv_connect_t    *connect = MVM_malloc(sizeof(uv_connect_t));
        int status;

        connect->data   = data;
        if ((status = uv_tcp_init(tc->loop, socket)) == 0 &&
                (status = uv_tcp_connect(connect, socket, dest, on_connect)) == 0) {
            uv_ref((uv_handle_t *)socket);
            uv_run(tc->loop, UV_RUN_DEFAULT);
            status = data->connect_status;
        }

        MVM_free(connect);
        MVM_free(dest);

        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket connect");

        data->handle = (uv_stream_t *)socket; /* So can be cleaned up in close */
        if (status < 0)
            MVM_exception_throw_adhoc(tc, "Failed to connect: %s", uv_strerror(status));
    }
    else {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket didn't connect");
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }
}

static void on_connection(uv_stream_t *server, int status) {
    /* Stash data for a later accept call (safe as we specify a queue of just
     * one connection). Decrement reference count also. */
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)server->data;
    data->accept_server = server;
    data->accept_status = status;
    uv_unref((uv_handle_t *)server);
}
static void socket_bind(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMint32 backlog) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    if (!data->handle) {
        struct sockaddr *dest    = MVM_io_resolve_host_name(tc, host, port);
        uv_tcp_t        *socket  = MVM_malloc(sizeof(uv_tcp_t));
        int r;

        if ((r = uv_tcp_init(tc->loop, socket)) != 0 ||
                (r = uv_tcp_bind(socket, dest, 0)) != 0) {
            MVM_free(socket);
            MVM_free(dest);
            MVM_exception_throw_adhoc(tc, "Failed to bind: %s", uv_strerror(r));
        }
        MVM_free(dest);

        /* Start listening, but unref the socket so it won't get in the way of
         * other things we want to do on this event loop. */
        socket->data = data;
        if ((r = uv_listen((uv_stream_t *)socket, backlog, on_connection)) != 0) {
            MVM_free(socket);
            MVM_exception_throw_adhoc(tc, "Failed to listen: %s", uv_strerror(r));
        }
        uv_unref((uv_handle_t *)socket);

        data->handle = (uv_stream_t *)socket;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }
}

MVMint64 socket_get_port(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;

    /* UV piece, we just need a FD */
    uv_tcp_t *socket = (uv_tcp_t *) data->handle;
    uv_os_fd_t fd;

    struct sockaddr_storage name;
    int error, len = sizeof(struct sockaddr_storage);
    MVMint64 port = 0;

    error = uv_fileno((uv_handle_t *) socket, &fd);

    if (error != 0)
        MVM_exception_throw_adhoc(tc, "Failed to get fileno from uv handle: %s", uv_strerror(error));

    error = getsockname(fd, (struct sockaddr *) &name, &len);

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

static void no_chars(MVMThreadContext *tc) {
        MVM_exception_throw_adhoc(tc, "Sockets no longer support string I/O at VM-level");
}
static void socket_set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding) {
    no_chars(tc);
}
static void socket_set_separator(MVMThreadContext *tc, MVMOSHandle *h, MVMString **seps, MVMint32 num_seps) {
    no_chars(tc);
}
static MVMString * socket_read_line(MVMThreadContext *tc, MVMOSHandle *h, MVMint32 chomp) {
    no_chars(tc);
}
static MVMString * socket_slurp(MVMThreadContext *tc, MVMOSHandle *h) {
    no_chars(tc);
}
static MVMString * socket_read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars) {
    no_chars(tc);
}
static MVMint64 socket_write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMString *str, MVMint64 newline) {
    no_chars(tc);
}

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h);

/* IO ops table, populated with functions. */
static const MVMIOClosable     closable      = { close_socket };
static const MVMIOSyncReadable sync_readable = { socket_set_separator,
                                                 socket_read_line,
                                                 socket_slurp,
                                                 socket_read_chars,
                                                 socket_read_bytes,
                                                 socket_eof };
static const MVMIOSyncWritable sync_writable = { socket_write_str,
                                                 socket_write_bytes,
                                                 socket_flush,
                                                 socket_truncate };
static const MVMIOSockety            sockety = { socket_connect,
                                                 socket_bind,
                                                 socket_accept,
                                                 socket_get_port };
static const MVMIOOps op_table = {
    &closable,
    NULL,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    NULL,
    &sockety,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncsocket accept");
    while (!data->accept_server) {
        if (tc->loop != data->handle->loop) {
            MVM_exception_throw_adhoc(tc, "Tried to accept() on a socket from outside its originating thread");
        }
        uv_ref((uv_handle_t *)data->handle);
        MVM_gc_mark_thread_blocked(tc);
        uv_run(tc->loop, UV_RUN_DEFAULT);
        MVM_gc_mark_thread_unblocked(tc);
    }

    /* Check the accept worked out. */
    if (data->accept_status < 0) {
        MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept failed");
        MVM_exception_throw_adhoc(tc, "Failed to listen: unknown error");
    }
    else {
        uv_tcp_t *client    = MVM_malloc(sizeof(uv_tcp_t));
        uv_stream_t *server = data->accept_server;
        int r;
        uv_tcp_init(tc->loop, client);
        data->accept_server = NULL;
        if ((r = uv_accept(server, (uv_stream_t *)client)) == 0) {
            MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
            MVMIOSyncSocketData * const data   = MVM_calloc(1, sizeof(MVMIOSyncSocketData));
            data->handle   = (uv_stream_t *)client;
            result->body.ops  = &op_table;
            result->body.data = data;
            MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept succeeded");
            return (MVMObject *)result;
        }
        else {
            uv_close((uv_handle_t*)client, NULL);
            MVM_free(client);
            MVM_telemetry_interval_stop(tc, interval_id, "syncsocket accept failed");
            MVM_exception_throw_adhoc(tc, "Failed to accept: %s", uv_strerror(r));
        }
    }
}

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen) {
    MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncSocketData * const data   = MVM_calloc(1, sizeof(MVMIOSyncSocketData));
    data->handle   = NULL;
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}

MVMString * MVM_io_get_hostname(MVMThreadContext *tc) {
    char hostname[65];
    gethostname(hostname, 65);
    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, hostname);
}
