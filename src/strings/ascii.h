MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, MVMObject *result_type, const char *ascii, size_t bytes);
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, const char *ascii);
MVMuint8 * MVM_string_ascii_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
MVMuint8 * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
MVMuint8 * MVM_string_ascii_encode_any(MVMThreadContext *tc, MVMString *str);
