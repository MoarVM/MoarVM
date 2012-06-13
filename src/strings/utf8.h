MVMString * MVM_string_utf8_decode(MVMThreadContext *tc, MVMObject *result_type, char *utf8, size_t bytes);
MVMuint8 * MVM_string_utf8_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
char * MVM_string_utf8_encode_C_string(MVMThreadContext *tc, MVMString *str);
