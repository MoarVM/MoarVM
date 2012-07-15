MVMString * MVM_string_latin1_decode(MVMThreadContext *tc, MVMObject *result_type, MVMuint8 *latin1, size_t bytes);
MVMuint8 * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
MVMuint32 latin1_cp_to_char(MVMint32 codepoint);
MVMint32 latin1_char_to_cp(MVMuint8 character);
