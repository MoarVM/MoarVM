/* Data that we keep for a stream-based handle. */
struct MVMIOSyncStreamData {
    /* is it a TTY? */
    MVMint8 is_tty;

    /* The libuv handle to the stream-readable thingy. */
    uv_stream_t *handle;

    /* The encoding we're using. */
    MVMint64 encoding;

    /* Did we reach EOF yet? */
    MVMint64 eof;

    /* Decode stream, for turning bytes into strings. */
    MVMDecodeStream *ds;

    /* The thread that is doing reading. */
    MVMThreadContext *cur_tc;

    /* Total bytes we've written. */
    MVMint64 total_bytes_written;

    /* Current separator specification for line-by-line reading. */
    MVMDecodeStreamSeparators sep_spec;

    /* Should we translate newlines? */
    MVMint32 translate_newlines;
};

void MVM_io_syncstream_set_encoding(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 encoding);
void MVM_io_syncstream_seek(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 offset, MVMint64 whence);
MVMint64 MVM_io_syncstream_tell(MVMThreadContext *tc, MVMOSHandle *h);
void MVM_io_syncstream_set_separator(MVMThreadContext *tc, MVMOSHandle *h, MVMString **sep, MVMint32 num_seps);
MVMString * MVM_io_syncstream_read_line(MVMThreadContext *tc, MVMOSHandle *h, MVMint32 chomp);
MVMString * MVM_io_syncstream_slurp(MVMThreadContext *tc, MVMOSHandle *h);
MVMString * MVM_io_syncstream_read_chars(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 chars);
MVMint64 MVM_io_syncstream_read_bytes(MVMThreadContext *tc, MVMOSHandle *h, char **buf, MVMint64 bytes);
MVMint64 MVM_io_syncstream_eof(MVMThreadContext *tc, MVMOSHandle *h);
MVMint64 MVM_io_syncstream_write_str(MVMThreadContext *tc, MVMOSHandle *h, MVMString *str, MVMint64 newline);
MVMint64 MVM_io_syncstream_write_bytes(MVMThreadContext *tc, MVMOSHandle *h, char *buf, MVMint64 bytes);
void MVM_io_syncstream_flush(MVMThreadContext *tc, MVMOSHandle *h);
void MVM_io_syncstream_truncate(MVMThreadContext *tc, MVMOSHandle *h, MVMint64 bytes);
MVMObject * MVM_io_syncstream_from_uvstream(MVMThreadContext *tc, uv_stream_t *handle, MVMint8 is_tty);
