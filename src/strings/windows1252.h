MVMString * MVM_string_windows1252_decode(MVMThreadContext *tc, const MVMObject *result_type, char *windows1252, size_t bytes);
MVM_PUBLIC MVMuint32 MVM_string_windows1252_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
char * MVM_string_windows1252_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
MVMString * MVM_string_windows1251_decode(MVMThreadContext *tc, const MVMObject *result_type, char *windows1252, size_t bytes);
MVM_PUBLIC MVMuint32 MVM_string_windows1251_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
char * MVM_string_windows1251_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
