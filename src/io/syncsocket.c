#include "moar.h"

#if defined(_MSC_VER)
#define snprintf _snprintf
#endif

 /* Data that we keep for a socket-based handle. */
typedef struct {
    /* Start with same fields as a sync stream, since we will re-use most
     * of its logic. */
    MVMIOSyncStreamData ss;
} MVMIOSyncSocketData;

static void do_close(MVMThreadContext *tc, MVMIOSyncSocketData *data) {
    if (data->ss.handle) {
         uv_close((uv_handle_t *)data->ss.handle, NULL);
         data->ss.handle = NULL;
    }
    if (data->ss.ds) {
        MVM_string_decodestream_destory(tc, data->ss.ds);
        data->ss.ds = NULL;
    }
}
static void close_socket(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    do_close(tc, data);
}

static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)d;
    do_close(tc, data);
}

static struct sockaddr * resolve_host_name(MVMThreadContext *tc, MVMString *host, MVMint64 port) {
    char *host_cstr = MVM_string_utf8_encode_C_string(tc, host);
    struct sockaddr *dest = malloc(sizeof(struct sockaddr));
    struct addrinfo *result;
    int error;
    char port_cstr[8];
    snprintf(port_cstr, 8, "%d", (int)port);

    error = getaddrinfo(host_cstr, port_cstr, NULL, &result);
    free(host_cstr);
    if (error == 0) {
        memcpy(dest, result->ai_addr, sizeof(struct sockaddr));
    }
    else {
        free(dest);
        MVM_exception_throw_adhoc(tc, "Failed to resolve host name");
    }
    freeaddrinfo(result);

    return dest;
}

static void on_connect(uv_connect_t* req, int status) {
    uv_unref((uv_handle_t *)req->handle);
    if (status < 0) {
        MVMThreadContext *tc = ((MVMIOSyncSocketData *)req->data)->ss.cur_tc;
        free(req);
        MVM_exception_throw_adhoc(tc, "Failed to connect: %s", uv_strerror(status));
    }
}
void socket_connect(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port) {
    MVMIOSyncSocketData *data = (MVMIOSyncSocketData *)h->body.data;
    if (!data->ss.handle) {
        struct sockaddr *dest    = resolve_host_name(tc, host, port);
        uv_tcp_t        *socket  = malloc(sizeof(uv_tcp_t));
        uv_connect_t    *connect = malloc(sizeof(uv_connect_t));
        int r;

        data->ss.cur_tc = tc;
        connect->data   = data;
        if ((r = uv_tcp_init(tc->loop, socket)) < 0 ||
                (r = uv_tcp_connect(connect, socket, dest, on_connect)) < 0) {
            free(socket);
            free(connect);
            free(dest);
            MVM_exception_throw_adhoc(tc, "Failed to connect: %s", uv_strerror(r));
        }
        uv_ref((uv_handle_t *)socket);
        uv_run(tc->loop, UV_RUN_DEFAULT);

        data->ss.handle = (uv_stream_t *)socket;

        free(connect);
        free(dest);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Socket already has a connection");
    }
}

void socket_bind(MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port) {
    MVM_exception_throw_adhoc(tc, "bind NYI");
}

MVMObject * socket_accept(MVMThreadContext *tc, MVMOSHandle *h) {
    MVM_exception_throw_adhoc(tc, "accept NYI");
}

/* IO ops table, populated with functions. */
static MVMIOClosable     closable      = { close_socket };
static MVMIOEncodable    encodable     = { MVM_io_syncstream_set_encoding };
static MVMIOSyncReadable sync_readable = { MVM_io_syncstream_set_separator,
                                           MVM_io_syncstream_read_line,
                                           MVM_io_syncstream_slurp,
                                           MVM_io_syncstream_read_chars,
                                           MVM_io_syncstream_read_bytes,
                                           MVM_io_syncstream_eof };
static MVMIOSyncWritable sync_writable = { MVM_io_syncstream_write_str,
                                           MVM_io_syncstream_write_bytes,
                                           MVM_io_syncstream_flush,
                                           MVM_io_syncstream_truncate };
static MVMIOSeekable     seekable      = { MVM_io_syncstream_seek,
                                           MVM_io_syncstream_tell };
static MVMIOSockety      sockety       = { socket_connect,
                                           socket_bind,
                                           socket_accept };
static MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    &seekable,
    &sockety,
    NULL,
    NULL,
    NULL,
    gc_free
};

MVMObject * MVM_io_socket_create(MVMThreadContext *tc, MVMint64 listen) {
    MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncSocketData * const data   = calloc(1, sizeof(MVMIOSyncSocketData));
    data->ss.handle   = NULL;
    data->ss.encoding = MVM_encoding_type_utf8;
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}

MVMString * MVM_io_get_hostname(MVMThreadContext *tc) {
    char hostname[65];
    gethostname(hostname, 65);
    return MVM_string_ascii_decode_nt(tc, tc->instance->VMString, hostname);
}
