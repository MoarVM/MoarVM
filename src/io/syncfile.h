/* Data that we keep for a file-based handle. */
struct MVMIOFileData {
    /* libuv file descriptor. */
    uv_file fd;

    /* The filename we opened, as a C string. */
    char *filename;

    /* The encoding we're using. */
    MVMint64 encoding;

    /* Decode stream, for turning bytes from disk into strings. */
    MVMDecodeStream *ds;

    /* Current separator codepoint. */
    MVMGrapheme32 sep;
};

MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMString *filename, MVMString *mode);
MVMObject * MVM_file_handle_from_fd(MVMThreadContext *tc, uv_file fd);
