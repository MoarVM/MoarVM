#include "moar.h"

/* We only get asynchronous forms of various kinds of I/O with libuv, yet we
 * also need to provide synchronous I/O on those. Here we do the work of that
 * adaptation. Since many things are exposed as streams in libuv, the code in
 * here is used to implement synchronous handling of TTYs, pipes, and sockets.
 * TTYs and (libuv) pipes use this directly; (real) pipes and sockets use many
 * of the functions, but have their own tables.
 */

/* Number of bytes we pull in at a time to the buffer. */
#define CHUNK_SIZE 65536

/* Cannot seek a TTY of named pipe (could fake the forward case, probably). */
void MVM_io_syncstream_seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence) {
    MVM_exception_throw_adhoc(tc, "Cannot seek this kind of handle");
}

/* If we've been reading, the total number of bytes read. Otherwise, the total
 * number of bytes we've written. */
MVMint64 MVM_io_syncstream_tell(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    return data->position;
}

/* Reads the specified number of bytes into a the supplied buffer, returing
 * the number actually read. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)handle->data;
    buf->base = MVM_malloc(data->to_read);
    buf->len = data->to_read;
    MVM_telemetry_interval_annotate(data->to_read, data->interval_id, "alloced this much space");
}
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)handle->data;
    if (nread > 0) {
        data->buf = buf->base;
        data->nread = nread;
    }
    else if (nread == UV_EOF) {
        data->buf = NULL;
        data->nread = 0;
        data->eof = 1;
        if (buf->base)
            MVM_free(buf->base);
    }
    uv_read_stop(handle);
    uv_unref((uv_handle_t*)handle);
}
MVMint64 MVM_io_syncstream_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf_out,
                                      MVMint64 bytes) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    if (bytes > 0 && !data->eof) {
        int r;
        unsigned int interval_id;

        interval_id = MVM_telemetry_interval_start(tc, "syncstream.read_bytes");
        data->handle->data = data;
        data->cur_tc = tc;
        data->to_read = bytes;
        if ((r = uv_read_start(data->handle, on_alloc, on_read)) < 0)
            MVM_exception_throw_adhoc(tc, "Reading from stream failed: %s",
                uv_strerror(r));
        uv_ref((uv_handle_t *)data->handle);
        if (tc->loop != data->handle->loop)
            MVM_exception_throw_adhoc(tc, "Tried to read() from an IO handle outside its originating thread");
        MVM_gc_mark_thread_blocked(tc);
        uv_run(tc->loop, UV_RUN_DEFAULT);
        MVM_gc_mark_thread_unblocked(tc);
        MVM_telemetry_interval_annotate(data->nread, data->interval_id, "read this many bytes");
        MVM_telemetry_interval_stop(tc, interval_id, "syncstream.read_to_buffer");
        *buf_out = data->buf;
        return data->nread;
    }
    else {
        *buf_out = NULL;
        return 0;
    }
}

/* Checks if the end of stream has been reached. */
MVMint64 MVM_io_syncstream_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    return data->eof;
}

/* Writes the specified bytes to the stream. */
static void write_cb(uv_write_t* req, int status) {
    uv_unref((uv_handle_t *)req->handle);
    MVM_free(req);
}
MVMint64 MVM_io_syncstream_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
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
    data->position += bytes;
    return bytes;
}

/* No flush available for stream. */
void MVM_io_syncstream_flush(MVMThreadContext *tc, MVMOSHandle *h){
}

/* Cannot truncate a stream. */
void MVM_io_syncstream_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes) {
    MVM_exception_throw_adhoc(tc, "Cannot truncate this kind of handle");
}

/* A close function for the case when we simply have a handle. */
static MVMint64 not_std_handle(MVMThreadContext *tc, MVMObject *h) {
    return h != tc->instance->stdin_handle &&
           h != tc->instance->stdout_handle &&
           h != tc->instance->stderr_handle;
}
static MVMint64 closefh(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    if (data->handle && not_std_handle(tc, (MVMObject *)h)) {
        uv_close((uv_handle_t *)data->handle, NULL);
        data->handle = NULL;
    }
    return 0;
}

static MVMint64 is_tty(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    return data->is_tty;
}

/* Get native file descriptor. */
static MVMint64 mvm_fileno(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t *)data->handle, &fd) >= 0)
        return (MVMint64)fd;
    return -1;
}

/* Operations aiding process spawning and I/O handling. */
static void bind_stdio_handle(MVMThreadContext *tc, MVMOSHandle *h, uv_stdio_container_t *stdio) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    stdio->flags              = UV_INHERIT_STREAM;
    stdio->data.stream        = data->handle;
}

/* Frees data associated with the handle, closing it if needed. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)d;
    if (data) {
        if (data->handle) {
            uv_close((uv_handle_t *)data->handle, NULL);
            uv_run(tc->loop, UV_RUN_DEFAULT);
            MVM_free(data->handle);
            data->handle = NULL;
        }
        MVM_free(data);
    }
}

/* IO ops table, populated with functions. */
static const MVMIOClosable     closable      = { closefh };
static const MVMIOSyncReadable sync_readable = { MVM_io_syncstream_read_bytes,
                                                 MVM_io_syncstream_eof };
static const MVMIOSyncWritable sync_writable = { MVM_io_syncstream_write_bytes,
                                                 MVM_io_syncstream_flush,
                                                 MVM_io_syncstream_truncate };
static const MVMIOSeekable          seekable = { MVM_io_syncstream_seek,
                                                 MVM_io_syncstream_tell };
static const MVMIOPipeable     pipeable      = { bind_stdio_handle };

static const MVMIOIntrospection introspection = { is_tty, mvm_fileno };

static const MVMIOOps op_table = {
    &closable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    &seekable,
    NULL,
    &pipeable,
    NULL,
    &introspection,
    NULL,
    gc_free
};

/* Wraps a libuv stream (likely, libuv pipe or TTY) up in a sync stream. */
MVMObject * MVM_io_syncstream_from_uvstream(MVMThreadContext *tc, uv_stream_t *handle, MVMint8 is_tty) {
    MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncStreamData * const data   = MVM_calloc(1, sizeof(MVMIOSyncStreamData));
    data->handle      = handle;
    data->is_tty      = is_tty;
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}
