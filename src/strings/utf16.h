MVMString * MVM_string_utf16_decode(MVMThreadContext *tc, MVMObject *result_type, MVMuint8 *utf16, size_t bytes);
MVMuint8 * MVM_string_utf16_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
MVMuint8 * MVM_string_utf16_encode(MVMThreadContext *tc, MVMString *str);
