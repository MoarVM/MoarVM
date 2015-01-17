MVMString * MVM_string_utf16_decode(MVMThreadContext *tc, MVMObject *result_type, char *utf16, size_t bytes);
char * MVM_string_utf16_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
char * MVM_string_utf16_encode(MVMThreadContext *tc, MVMString *str);
