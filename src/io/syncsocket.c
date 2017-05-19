#include "moar.h"

#ifndef _WIN32
    #include "unistd.h"
#endif

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

/* Number of bytes we read at a time to the buffer (equal to largest TCP
 * packet size). */
#define CHUNK_SIZE 65536

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* Start with same fields as a sync stream, since we will re-use most
     * of its logic. */
    MVMIOSyncStreamData ss;

    /* Status of the last connect attempt, if any. */
    int connect_status;

    /* Details of next connection to accept; NULL if none. */
    uv_stream_t *accept_server;
    int          accept_status;
} MVMIOSyncSocketData;

/* Read a bunch of bytes into the current decode stream. Returns true if we
 * read some data, and false if we hit EOF. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)handle->data;
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = MVM_malloc(size);
    MVM_telemetry_interval_annotate(size, data->interval_id, "alloced this much space");
    buf->len    = size;
}
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)handle->data;
    if (nread > 0) {
        MVM_string_decodestream_add_bytes(data->cur_tc, data->ds, buf->base, nread);
        MVM_telemetry_interval_annotate(nread, data->interval_id, "read this many bytes");
    }
    else if (nread == UV_EOF) {
        data->eof = 1;
        if (buf->base)
            MVM_free(buf->base);
    }
    uv_read_stop(handle);
    uv_unref((uv_handle_t*)handle);
}
static MVMint32 read_to_buffer(MVMThreadContext *tc, MVMIOSyncStreamData *data, MVMint32 bytes) {
    /* Don't try and read again if we already saw EOF. */
    if (!data->eof) {
        int r;
        unsigned int interval_id;

        interval_id = MVM_telemetry_interval_start(tc, "syncstream.read_to_buffer");
        data->handle->data = data;
        data->cur_tc = tc;
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
        MVM_telemetry_interval_stop(tc, interval_id, "syncstream.read_to_buffer");
        return 1;
    }
    else {
        return 0;
    }
}

/* Ensures we have a decode stream, creating it if we're missing one. */
static void ensure_decode_stream(MVMThreadContext *tc, MVMIOSyncStreamData *data) {
    if (!data->ds)
        data->ds = MVM_string_decodestream_create(tc, data->encoding, 0,
            data->translate_newlines);
}

MVMint64 socket_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* See if we've already enough; if not, try and grab more. */
    if (!MVM_string_decodestream_have_bytes(tc, data->ds, bytes))
        read_to_buffer(tc, data, bytes > CHUNK_SIZE ? bytes : CHUNK_SIZE);

    /* Read as many as we can, up to the limit. */
    return MVM_string_decodestream_bytes_to_buf(tc, data->ds, buf, bytes);
}

/* Checks if the end of stream has been reached. */
MVMint64 socket_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;

    /* If we still have stuff in the buffer, certainly not the end (even if
     * data->eof is set; that just means we read all we can from libuv, not
     * that we processed it all). */
    if (data->ds && !MVM_string_decodestream_is_empty(tc, data->ds))
        return 0;

    /* Otherwise, go on the EOF flag from the underlying stream. */
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
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    uv_write_t *req = MVM_malloc(sizeof(uv_write_t));
    uv_buf_t write_buf = uv_buf_init(buf, bytes);
    int r;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "syncstream.write_bytes");
    uv_ref((uv_handle_t *)data->handle);
    if ((r = uv_write(req, data->handle, &write_buf, 1, write_cb)) < 0) {
        uv_unref((uv_handle_t *)data->handle);
        MVM_free(req);
        MVM_telemetry_interval_stop(tc, interval_id, "syncstream.write_bytes failed");
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to stream: %s", uv_strerror(r));
    }
    else {
        MVM_gc_mark_thread_blocked(tc);
        uv_run(tc->loop, UV_RUN_DEFAULT);
        MVM_gc_mark_thread_unblocked(tc);
    }
    MVM_telemetry_interval_annotate(bytes, interval_id, "written this many bytes");
    MVM_telemetry_interval_stop(tc, interval_id, "syncstream.write_bytes");
    data->total_bytes_written += bytes;
    return bytes;
}

static void free_on_close_cb(uv_handle_t *handle) {
    MVM_free(handle);
}
static MVMint64 do_close(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    if (data->ss.handle) {
         uv_close((uv_handle_t *)data->ss.handle, free_on_close_cb);
         data->ss.handle = NULL;
    }
    if (data->ss.ds) {
        MVM_string_decodestream_destroy(tc, data->ss.ds);
        data->ss.ds = NULL;
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
    MVM_string_decode_stream_sep_destroy(tc, &(data->ss.sep_spec));
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
    if (!data->ss.handle) {
        struct sockaddr *dest    = MVM_io_resolve_host_name(tc, host, port);
        uv_tcp_t        *socket  = MVM_malloc(sizeof(uv_tcp_t));
        uv_connect_t    *connect = MVM_malloc(sizeof(uv_connect_t));
        int status;

        data->ss.cur_tc = tc;
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

        data->ss.handle = (uv_stream_t *)socket; /* So can be cleaned up in close */
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
    if (!data->ss.handle) {
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

        data->ss.handle = (uv_stream_t *)socket;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Socket is already bound or connected");
    }
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
                                                 socket_accept };
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
        if (tc->loop != data->ss.handle->loop) {
            MVM_exception_throw_adhoc(tc, "Tried to accept() on a socket from outside its originating thread");
        }
        uv_ref((uv_handle_t *)data->ss.handle);
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
            data->ss.handle   = (uv_stream_t *)client;
            data->ss.encoding = MVM_encoding_type_utf8;
            MVM_string_decode_stream_sep_default(tc, &(data->ss.sep_spec));
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
    data->ss.handle   = NULL;
    data->ss.encoding = MVM_encoding_type_utf8;
    data->ss.translate_newlines = 0;
    MVM_string_decode_stream_sep_default(tc, &(data->ss.sep_spec));
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}

MVMString * MVM_io_get_hostname(MVMThreadContext *tc) {
    char hostname[65];
    gethostname(hostname, 65);
    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, hostname);
}
