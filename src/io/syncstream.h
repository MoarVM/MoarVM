/* Data that we keep for a stream-based handle. */
struct MVMIOSyncStreamData {
    /* The libuv handle to the stream-readable thingy. */
    uv_stream_t *handle;

    /* Bits of state we need in here while we still are using libuv. */
    MVMThreadContext *cur_tc;
    size_t to_read;
    size_t nread;
    char *buf;

    /* Did we reach EOF yet? */
    MVMint64 eof;

    /* Total bytes we've read or written. */
    MVMint64 position;

    /* is it a TTY? */
    MVMint8 is_tty;

    unsigned int interval_id;
};

void MVM_io_syncstream_seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence);
MVMint64 MVM_io_syncstream_tell(MVMThreadContext *tc, MVMOSHandle *h);
MVMint64 MVM_io_syncstream_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes);
MVMint64 MVM_io_syncstream_eof(MVMThreadContext *tc, MVMOSHandle *h);
MVMint64 MVM_io_syncstream_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes);
void MVM_io_syncstream_flush(MVMThreadContext *tc, MVMOSHandle *h);
void MVM_io_syncstream_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes);
