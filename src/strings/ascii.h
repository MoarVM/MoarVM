MVMString * MVM_string_ascii_decode(MVMThreadContext *tc, MVMObject *result_type, char *ascii, size_t bytes);
MVMString * MVM_string_ascii_decode_nt(MVMThreadContext *tc, MVMObject *result_type, char *ascii);
MVMuint8 * MVM_string_ascii_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
