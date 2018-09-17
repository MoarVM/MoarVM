MVMString * MVM_string_utf16_decode(MVMThreadContext *tc, const MVMObject *result_type, MVMuint8 *utf16, size_t bytes);
MVMString * MVM_string_utf16le_decode(MVMThreadContext *tc, const MVMObject *result_type, MVMuint8 *utf16, size_t bytes);
MVMString * MVM_string_utf16be_decode(MVMThreadContext *tc, const MVMObject *result_type, MVMuint8 *utf16, size_t bytes);
char * MVM_string_utf16_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
char * MVM_string_utf16le_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
char * MVM_string_utf16be_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
char * MVM_string_utf16_encode(MVMThreadContext *tc, MVMString *str, MVMint32 translate_newlines);
MVMuint32 MVM_string_utf16_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
