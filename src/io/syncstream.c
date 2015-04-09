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

/* Sets the encoding used for string-based I/O. */
void MVM_io_syncstream_set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    if (data->ds) {
        if (data->ds->chars_head)
            MVM_exception_throw_adhoc(tc, "Too late to change handle encoding");
        data->ds->encoding = encoding;
    }
    data->encoding = encoding;
}

/* Cannot seek a TTY of named pipe (could fake the forward case, probably). */
void MVM_io_syncstream_seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence) {
    MVM_exception_throw_adhoc(tc, "Cannot seek this kind of handle");
}

/* If we've been reading, the total number of bytes read. Otherwise, the total
 * number of bytes we've written. */
MVMint64 MVM_io_syncstream_tell(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    return data->ds
        ? MVM_string_decodestream_tell_bytes(tc, data->ds)
        : data->total_bytes_written;
}

/* Set the line separator. */
void MVM_io_syncstream_set_separator(MVMThreadContext *tc, MVMOSHandle *h, MVMString *sep) {
    /* For now, take last character. */
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    data->sep = (MVMGrapheme32)MVM_string_get_grapheme_at(tc, sep,
        MVM_string_graphs(tc, sep) - 1);
}

/* Read a bunch of bytes into the current decode stream. Returns true if we
 * read some data, and false if we hit EOF. */
static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    size_t size = suggested_size > 0 ? suggested_size : 4;
    buf->base   = MVM_malloc(size);
    buf->len    = size;
}
static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)handle->data;
    if (nread > 0) {
        MVM_string_decodestream_add_bytes(data->cur_tc, data->ds, buf->base, nread);
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
        data->handle->data = data;
        data->cur_tc = tc;
        if ((r = uv_read_start(data->handle, on_alloc, on_read)) < 0)
            MVM_exception_throw_adhoc(tc, "Reading from stream failed: %s",
                uv_strerror(r));
        uv_ref((uv_handle_t *)data->handle);
        uv_run(tc->loop, UV_RUN_DEFAULT);
        return 1;
    }
    else {
        return 0;
    }
}

/* Ensures we have a decode stream, creating it if we're missing one. */
static void ensure_decode_stream(MVMThreadContext *tc, MVMIOSyncStreamData *data) {
    if (!data->ds)
        data->ds = MVM_string_decodestream_create(tc, data->encoding, 0);
}

/* Reads a single line from the stream. May serve it from a buffer, if we
 * already read enough data. */
MVMString * MVM_io_syncstream_read_line(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* Pull data until we can read a line. */
    do {
        MVMString *line = MVM_string_decodestream_get_until_sep(tc, data->ds, data->sep);
        if (line != NULL)
            return line;
    } while (read_to_buffer(tc, data, CHUNK_SIZE) > 0);

    /* Reached end of stream, or last (non-termianted) line. */
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Reads the stream from the current position to the end into a string,
 * fetching as much data is available. */
MVMString * MVM_io_syncstream_slurp(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* Fetch as much data as we can (XXX this can be more efficient, by
     * passing on down that we want to get many buffers from libuv). */
    while (read_to_buffer(tc, data, CHUNK_SIZE))
        ;
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Gets the specified number of characters from the stream. */
MVMString * MVM_io_syncstream_read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    MVMString *result;
    ensure_decode_stream(tc, data);

    /* Do we already have the chars available? */
    result = MVM_string_decodestream_get_chars(tc, data->ds, chars);
    if (result) {
        return result;
    }
    else {
        /* No; read and try again. */
        read_to_buffer(tc, data, CHUNK_SIZE);
        result = MVM_string_decodestream_get_chars(tc, data->ds, chars);
        if (result != NULL)
            return result;
    }

    /* Fetched all we immediately can, so just take what we have. */
    return MVM_string_decodestream_get_all(tc, data->ds);
}

/* Reads the specified number of bytes into a the supplied buffer, returing
 * the number actually read. */
MVMint64 MVM_io_syncstream_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    ensure_decode_stream(tc, data);

    /* See if we've already enough; if not, try and grab more. */
    if (!MVM_string_decodestream_have_bytes(tc, data->ds, bytes))
        read_to_buffer(tc, data, bytes > CHUNK_SIZE ? bytes : CHUNK_SIZE);

    /* Read as many as we can, up to the limit. */
    return MVM_string_decodestream_bytes_to_buf(tc, data->ds, buf, bytes);
}

/* Checks if the end of stream has been reached. */
MVMint64 MVM_io_syncstream_eof(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;

    /* If we still have stuff in the buffer, certainly not the end (even if
     * data->eof is set; that just means we read all we can from libuv, not
     * that we processed it all). */
    if (data->ds && !MVM_string_decodestream_is_empty(tc, data->ds))
        return 0;

    /* Otherwise, go on the EOF flag from the underlying stream. */
    return data->eof;
}

/* Writes the specified string to the stream, maybe with a newline. */
static void write_cb(uv_write_t* req, int status) {
    uv_unref((uv_handle_t *)req->handle);
    MVM_free(req);
}
MVMint64 MVM_io_syncstream_write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMString *str, MVMint64 newline) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    char *output;
    MVMuint64 output_size;
    uv_write_t *req;
    uv_buf_t write_buf;
    int r;

    output = MVM_string_encode(tc, str, 0, -1, &output_size, data->encoding);
    if (newline) {
        output = (char *)MVM_realloc(output, ++output_size);
        output[output_size - 1] = '\n';
    }
    req = MVM_malloc(sizeof(uv_write_t));
    write_buf = uv_buf_init(output, output_size);
    uv_ref((uv_handle_t *)data->handle);
    if ((r = uv_write(req, data->handle, &write_buf, 1, write_cb)) < 0) {
        uv_unref((uv_handle_t *)data->handle);
        MVM_free(req);
        MVM_free(output);
        MVM_exception_throw_adhoc(tc, "Failed to write string to stream: %s", uv_strerror(r));
    }
    else {
        uv_run(tc->loop, UV_RUN_DEFAULT);
        MVM_free(output);
    }

    data->total_bytes_written += output_size;
    return output_size;
}

/* Writes the specified bytes to the stream. */
MVMint64 MVM_io_syncstream_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)h->body.data;
    uv_write_t *req = MVM_malloc(sizeof(uv_write_t));
    uv_buf_t write_buf = uv_buf_init(buf, bytes);
    int r;
    uv_ref((uv_handle_t *)data->handle);
    if ((r = uv_write(req, data->handle, &write_buf, 1, write_cb)) < 0) {
        uv_unref((uv_handle_t *)data->handle);
        MVM_free(req);
        MVM_exception_throw_adhoc(tc, "Failed to write bytes to stream: %s", uv_strerror(r));
    }
    else {
        uv_run(tc->loop, UV_RUN_DEFAULT);
    }
    data->total_bytes_written += bytes;
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
         if (data->ds) {
            MVM_string_decodestream_destory(tc, data->ds);
            data->ds = NULL;
        }
    }
    return 0;
}

/* Frees data associated with the handle, closing it if needed. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncStreamData *data = (MVMIOSyncStreamData *)d;
    if (data) {
        if (data->handle && not_std_handle(tc, h)) {
            uv_close((uv_handle_t *)data->handle, NULL);
            data->handle = NULL;
        }
        if (data->ds) {
            MVM_string_decodestream_destory(tc, data->ds);
            data->ds = NULL;
        }
        MVM_free(data);
    }
}

/* IO ops table, populated with functions. */
static const MVMIOClosable     closable      = { closefh };
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
static const MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    &seekable,
    NULL,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Wraps a libuv stream (likely, libuv pipe or TTY) up in a sync stream. */
MVMObject * MVM_io_syncstream_from_uvstream(MVMThreadContext *tc, uv_stream_t *handle) {
    MVMOSHandle         * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncStreamData * const data   = MVM_calloc(1, sizeof(MVMIOSyncStreamData));
    data->handle      = handle;
    data->encoding    = MVM_encoding_type_utf8;
    data->sep         = '\n';
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}
