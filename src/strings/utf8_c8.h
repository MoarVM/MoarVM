MVM_PUBLIC MVMString * MVM_string_utf8_c8_decode(MVMThreadContext *tc, const MVMObject *result_type, const char *utf8, size_t bytes);
MVM_PUBLIC void MVM_string_utf8_c8_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
MVM_PUBLIC char * MVM_string_utf8_c8_encode_substr(MVMThreadContext *tc,
        MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement);
MVM_PUBLIC char * MVM_string_utf8_c8_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
