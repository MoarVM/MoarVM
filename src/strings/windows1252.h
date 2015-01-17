MVMString * MVM_string_windows1252_decode(MVMThreadContext *tc, MVMObject *result_type, char *windows1252, size_t bytes);
char * MVM_string_windows1252_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
