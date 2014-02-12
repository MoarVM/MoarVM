/* Operation table for I/O. A given handle type may implement any number of
 * these sections. */
struct MVMIOOps {
    /* The various sections that may be implemented. */
    MVMIOClosable     *closable;
    MVMIOEncodable    *encodable;
    MVMIOSyncReadable *sync_readable;
    MVMIOSyncWritable *sync_writable;
    MVMIOSeekable     *seekable;
    MVMIOBindable     *bindable;
    MVMIOInteractive  *interactive;
    MVMIOLockable     *lockable;

    /* How to mark the handle's data, if needed. */
    void (*gc_mark) (MVMThreadContext *tc, void *data, MVMGCWorklist *worklist);

    /* How to free the handle's data. */
    void (*gc_free) (MVMThreadContext *tc, void *data);
};

/* I/O operations on handles that can be closed. */
struct MVMIOClosable {
    void (*close) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can do encoding to/from MVMString. */
struct MVMIOEncodable {
    void (*set_encoding) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding);
};

/* I/O operations on handles that can do synchronous reading. */
struct MVMIOSyncReadable {
    void (*set_separator) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *sep);
    MVMString * (*read_line) (MVMThreadContext *tc, MVMOSHandle *h);
    MVMString * (*slurp) (MVMThreadContext *tc, MVMOSHandle *h);
    MVMString * (*read_chars) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars);
    MVMint64 (*read_bytes) (MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes);
    MVMint64 (*eof) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can do synchronous writing. */
struct MVMIOSyncWritable {
    MVMint64 (*write_str) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *s, MVMint64 newline);
    MVMint64 (*write_bytes) (MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes);
    void (*flush) (MVMThreadContext *tc, MVMOSHandle *h);
    void (*truncate) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes);
};

/* I/O operations on handles that can seek/tell. */
struct MVMIOSeekable {
    void (*seek) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence);
    MVMint64 (*tell) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can bind. */
struct MVMIOBindable {
    void (*bind) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *host, MVMint64 port);
    MVMObject * (*accept) (MVMThreadContext *tc, MVMOSHandle *h);
};

/* I/O operations on handles that can do interactive readline. */
struct MVMIOInteractive {
    MVMString * (*read_line) (MVMThreadContext *tc, MVMOSHandle *h, MVMString *prompt);
};

/* I/O operations on handles that can lock/unlock. */
struct MVMIOLockable {
    MVMint64 (*lock) (MVMThreadContext *tc, MVMOSHandle *h, MVMint64 flag);
    void (*unlock) (MVMThreadContext *tc, MVMOSHandle *h);
};
