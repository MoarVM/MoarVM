#include "moar.h"

#ifndef _WIN32
    #include "unistd.h"
#endif

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* Start with same fields as a sync stream, since we will re-use most
     * of its logic. */
    MVMIOSyncStreamData ss;

    /* Details of next connection to accept; NULL if none. */
    uv_stream_t *accept_server;
    int          accept_status;
} MVMIOSyncSocketData;

static MVMint64 do_close(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    if (data->ss.handle) {
         uv_close((uv_handle_t *)data->ss.handle, NULL);
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
    uv_unref((uv_handle_t *)req->handle);
    if (status < 0) {
        MVMThreadContext *tc = ((MVMIOSyncSocketData *)req->data)->ss.cur_tc;
        MVM_free(req);
        MVM_exception_throw_adhoc(tc, "Failed to connect: %s", uv_strerror(status));
    }
}
static void socket_connect(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    if (!data->ss.handle) {
        struct sockaddr *dest    = MVM_io_resolve_host_name(tc, host, port);
        uv_tcp_t        *socket  = MVM_malloc(sizeof(uv_tcp_t));
        uv_connect_t    *connect = MVM_malloc(sizeof(uv_connect_t));
        int r;

        data->ss.cur_tc = tc;
        connect->data   = data;
        if ((r = uv_tcp_init(tc->loop, socket)) < 0 ||
                (r = uv_tcp_connect(connect, socket, dest, on_connect)) < 0) {
            MVM_free(socket);
            MVM_free(connect);
            MVM_free(dest);
            MVM_exception_throw_adhoc(tc, "Failed to connect: %s", uv_strerror(r));
        }
        uv_ref((uv_handle_t *)socket);
        uv_run(tc->loop, UV_RUN_DEFAULT);

        data->ss.handle = (uv_stream_t *)socket;

        MVM_free(connect);
        MVM_free(dest);
    }
    else {
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

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h);

/* IO ops table, populated with functions. */
static const MVMIOClosable     closable      = { close_socket };
static const MVMIOEncodable    encodable     = { MVM_io_syncstream_set_encoding };
static const MVMIOSyncReadable sync_readable = { MVM_io_syncstream_set_separator,
                                                 MVM_io_syncstream_read_line,
                                                 MVM_io_syncstream_slurp,
                                                 MVM_io_syncstream_read_chars,
                                                 MVM_io_syncstream_read_bytes,
                                                 MVM_io_syncstream_eof };
static const MVMIOSyncWritable sync_writable = { MVM_io_syncstream_write_str,
                                                 MVM_io_syncstream_write_bytes,
                                                 MVM_io_syncstream_flush,
                                                 MVM_io_syncstream_truncate };
static const MVMIOSeekable          seekable = { MVM_io_syncstream_seek,
                                                 MVM_io_syncstream_tell };
static const MVMIOSockety            sockety = { socket_connect,
                                                 socket_bind,
                                                 socket_accept };
static const MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    &seekable,
    &sockety,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

static MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;

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
            return (MVMObject *)result;
        }
        else {
            uv_close((uv_handle_t*)client, NULL);
            MVM_free(client);
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
