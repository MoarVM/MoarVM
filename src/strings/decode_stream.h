/* Represents a bytes => chars decoding stream. */
struct MVMDecodeStream {
    /* Head and tail of the input byte buffers. */
    MVMDecodeStreamBytes *bytes_head;
    MVMDecodeStreamBytes *bytes_tail;

    /* Head and tail of the output char buffers. */
    MVMDecodeStreamChars *chars_head;
    MVMDecodeStreamChars *chars_tail;

    /* The byte position (for tell). */
    MVMint64 abs_byte_pos;

    /* How far we've eaten into the current head bytes buffer. */
    MVMint32 bytes_head_pos;

    /* How far we've eaten into the current head char buffer. */
    MVMint32 chars_head_pos;

    /* The encoding we're using. */
    MVMint32 encoding;

    /* Normalizer. */
    MVMNormalizer norm;
};

/* A single bunch of bytes added to a decode stream, with a link to the next
 * one, if any. */
struct MVMDecodeStreamBytes {
    char                 *bytes;
    MVMint32              length;
    MVMDecodeStreamBytes *next;
};

/* A bunch of characters already decoded, with a link to the next bunch. */
struct MVMDecodeStreamChars {
    MVMGrapheme32        *chars;
    MVMint32              length;
    MVMDecodeStreamChars *next;
};

MVMDecodeStream * MVM_string_decodestream_create(MVMThreadContext *tc, MVMint32 encoding, MVMint64 abs_byte_pos);
void MVM_string_decodestream_add_bytes(MVMThreadContext *tc, MVMDecodeStream *ds, char *bytes, MVMint32 length);
void MVM_string_decodestream_add_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMGrapheme32 *chars, MVMint32 length);
void MVM_string_decodestream_discard_to(MVMThreadContext *tc, MVMDecodeStream *ds, MVMDecodeStreamBytes *bytes, MVMint32 pos);
MVMString * MVM_string_decodestream_get_chars(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 chars);
MVMString * MVM_string_decodestream_get_until_sep(MVMThreadContext *tc, MVMDecodeStream *ds, MVMGrapheme32 sep);
MVMString * MVM_string_decodestream_get_all(MVMThreadContext *tc, MVMDecodeStream *ds);
MVMint64 MVM_string_decodestream_have_bytes(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 bytes);
MVMint64 MVM_string_decodestream_bytes_to_buf(MVMThreadContext *tc, MVMDecodeStream *ds, char **buf, MVMint32 bytes);
MVMint64 MVM_string_decodestream_tell_bytes(MVMThreadContext *tc, MVMDecodeStream *ds);
MVMint32 MVM_string_decodestream_is_empty(MVMThreadContext *tc, MVMDecodeStream *ds);
void MVM_string_decodestream_destory(MVMThreadContext *tc, MVMDecodeStream *ds);
