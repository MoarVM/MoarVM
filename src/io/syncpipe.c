#include "moar.h"

#ifndef _WIN32
    #include <sys/wait.h>
#endif

/* This heavily re-uses the logic from syncstream, but with different close
 * and gc_free semantics. */

/* Closes the pipe. */
static MVMint64 do_close(MVMThreadContext *tc, MVMIOSyncPipeData *data) {
#ifdef _WIN32
    DWORD status = 0;
#else
    int status = 0;
#endif
    if (data->ss.handle == NULL || uv_is_closing((uv_handle_t*)data->ss.handle))
        return 0;
    /* closing the in-/output std filehandle will shutdown the child process. */
    uv_unref((uv_handle_t*)data->ss.handle);
    uv_close((uv_handle_t*)data->ss.handle, NULL);
    if (data->process) {
#ifdef _WIN32
        if (!uv_is_closing((uv_handle_t*)data->process))
            uv_process_close(tc->loop, data->process);
        GetExitCodeProcess(data->process->process_handle, &status);
        status = status << 8;
#else
        pid_t wpid;
        do
            wpid = waitpid(data->process->pid, &status, 0);
        while (wpid == -1 && errno == EINTR);
#endif
    }
    if (!status && data->process->data) {
        status = *(MVMint64*)data->process->data;
        MVM_free(data->process->data);
        data->process->data = NULL;
    }
    uv_unref((uv_handle_t *)data->process);
    uv_run(tc->loop, UV_RUN_DEFAULT);
    data->process   = NULL;
    data->ss.handle = NULL;
    if (data->ss.ds) {
        MVM_string_decodestream_destory(tc, data->ss.ds);
        data->ss.ds = NULL;
    }
    return (MVMint64)status;
}
static MVMint64 closefh(MVMThreadContext *tc, MVMOSHandle *h) {
    MVMIOSyncPipeData *data = (MVMIOSyncPipeData *)h->body.data;
    return do_close(tc, data);
}

/* Operations aiding process spawning and I/O handling. */
static void bind_stdio_handle(MVMThreadContext *tc, MVMOSHandle *h, uv_stdio_container_t *stdio,
        uv_process_t *process) {
    MVMIOSyncPipeData *data = (MVMIOSyncPipeData *)h->body.data;
    data->process           = process;
    stdio->flags            = UV_INHERIT_STREAM;
    stdio->data.stream      = data->ss.handle;
}

/* Frees data associated with the pipe, closing it if needed. */
static void gc_free(MVMThreadContext *tc, MVMObject *h, void *d) {
    MVMIOSyncPipeData *data = (MVMIOSyncPipeData *)d;
    do_close(tc, data);
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
static const MVMIOPipeable     pipeable      = { bind_stdio_handle };
static const MVMIOOps op_table = {
    &closable,
    &encodable,
    &sync_readable,
    &sync_writable,
    NULL,
    NULL,
    NULL,
    &seekable,
    NULL,
    &pipeable,
    NULL,
    NULL,
    NULL,
    gc_free
};

/* Creates a sync pipe handle. */
MVMObject * MVM_io_syncpipe(MVMThreadContext *tc) {
    MVMOSHandle       * const result = (MVMOSHandle *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTIO);
    MVMIOSyncPipeData * const data   = MVM_calloc(1, sizeof(MVMIOSyncPipeData));
    uv_pipe_t *handle = MVM_malloc(sizeof(uv_pipe_t));
    uv_pipe_init(tc->loop, handle, 0);
    data->ss.handle   = (uv_stream_t *)handle;
    data->ss.encoding = MVM_encoding_type_utf8;
    MVM_string_decode_stream_sep_default(tc, &(data->ss.sep_spec));
    result->body.ops  = &op_table;
    result->body.data = data;
    return (MVMObject *)result;
}
