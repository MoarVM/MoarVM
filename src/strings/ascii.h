MVM_PUBLIC MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, const MVMObject *result_type, const char *ascii, size_t bytes);
MVM_PUBLIC MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, const MVMObject *result_type, const char *ascii);
MVM_PUBLIC void MVM_string_ascii_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
MVM_PUBLIC char * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement);
MVM_PUBLIC char * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
char * MVM_string_ascii_encode_any(MVMThreadContext *tc, MVMString *str);
