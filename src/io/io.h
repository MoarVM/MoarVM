/* Operation table for I/O. A given handle type may implement any number of
 * these sections. */
struct MVMIOOps {
    /* The various sections that may be implemented. */
    const MVMIOClosable        *closable;
    const MVMIOSyncReadable    *sync_readable;
    const MVMIOSyncWritable    *sync_writable;
    const MVMIOAsyncReadable   *async_readable;
    const MVMIOAsyncWritable   *async_writable;
    const MVMIOAsyncWritableTo *async_writable_to;
    const MVMIOSeekable        *seekable;
    const MVMIOSockety         *sockety;
    MVMObject * (*get_async_task_handle) (MVMThreadContext *tc, MVMOSHandle *h);
    const MVMIOLockable        *lockable;
    const MVMIOIntrospection   *introspection;
    void (*set_buffer_size) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 size);

    /* How to mark the handle's data, if needed. */
    void (*gc_mark) (MVMThreadContext *tc, void *data, MVMGCWorklist *worklist);

    /* How to free the handle's data. */
    void (*gc_free) (MVMThreadContext *tc, MVMObject *h, void *data);
};

/* I/O operations on handles that can be closed. */
struct MVMIOClosable {
    MVMint64 (*close) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can do synchronous reading. */
struct MVMIOSyncReadable {
    MVMint64 (*read_bytes) (MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMuint64 bytes);
    MVMint64 (*eof) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can do synchronous writing. */
struct MVMIOSyncWritable {
    MVMint64 (*write_bytes) (MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMuint64 bytes);
    void (*flush) (MVMThreadContext *tc, MVMOSHandle *h, MVMint32 sync);
    void (*truncate) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes);
};

/* I/O operations on handles that can do asynchronous reading. */
struct MVMIOAsyncReadable {
    MVMAsyncTask * (*read_bytes) (MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
        MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type);
};

/* I/O operations on handles that can do asynchronous writing. */
struct MVMIOAsyncWritable {
    MVMAsyncTask * (*write_bytes) (MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
        MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type);
};

/* I/O operations on handles that can do asynchronous writing to a given
 * network destination. */
struct MVMIOAsyncWritableTo {
    MVMAsyncTask * (*write_bytes_to) (MVMThreadContext *tc, MVMOSHandle *h, MVMObject *queue,
        MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type, MVMString *host, MVMint64 port);
};

/* I/O operations on handles that can seek/tell. */
struct MVMIOSeekable {
    void (*seek) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence);
    MVMint64 (*tell) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that do socket-y things (connect, bind, accept). */
struct MVMIOSockety {
    void (*connect) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family);
    void (*bind) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port, MVMuint16 family, MVMint32 backlog);
    MVMObject * (*accept) (MVMThreadContext *tc, MVMOSHandle *h);
    MVMint64 (*getport) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can lock/unlock. */
struct MVMIOLockable {
    MVMint64 (*lock) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 flag);
    void (*unlock) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* Various bits of introspection we can perform on a handle. */
struct MVMIOIntrospection {
    MVMint64 (*is_tty) (MVMThreadContext *tc, MVMOSHandle *h);
    MVMint64 (*native_descriptor) (MVMThreadContext *tc, MVMOSHandle *h);
};

MVMint64 MVM_io_close(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_io_is_tty(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_io_fileno(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_io_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag);
MVMint64 MVM_io_tell(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_io_read_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *result, MVMint64 length);
void MVM_io_write_bytes(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *buffer);
void MVM_io_write_bytes_c(MVMThreadContext *tc, MVMObject *oshandle, char *output,
    MVMuint64 output_size);
MVMObject * MVM_io_read_bytes_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
    MVMObject *schedulee, MVMObject *buf_type, MVMObject *async_type);
MVMObject * MVM_io_write_bytes_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
        MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type);
MVMObject * MVM_io_write_bytes_to_async(MVMThreadContext *tc, MVMObject *oshandle, MVMObject *queue,
        MVMObject *schedulee, MVMObject *buffer, MVMObject *async_type, MVMString *host, MVMint64 port);
MVMint64 MVM_io_eof(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_io_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag);
void MVM_io_unlock(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_io_flush(MVMThreadContext *tc, MVMObject *oshandle, MVMint32 sync);
void MVM_io_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset);
void MVM_io_connect(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port, MVMuint16 family);
void MVM_io_bind(MVMThreadContext *tc, MVMObject *oshandle, MVMString *host, MVMint64 port, MVMuint16 family, MVMint32 backlog);
MVMObject * MVM_io_accept(MVMThreadContext *tc, MVMObject *oshandle);
MVMint64 MVM_io_getport(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_io_set_buffer_size(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 size);
MVMObject * MVM_io_get_async_task_handle(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_io_flush_standard_handles(MVMThreadContext *tc);
