MVM_PUBLIC MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, MVMObject *result_type, const MVMint8 *ascii, size_t bytes);
MVM_PUBLIC MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, const char *ascii);
MVM_PUBLIC void MVM_string_ascii_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 *stopper_chars, MVMint32 *stopper_sep);
MVM_PUBLIC MVMuint8 * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
MVM_PUBLIC MVMuint8 * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
MVMuint8 * MVM_string_ascii_encode_any(MVMThreadContext *tc, MVMString *str);
