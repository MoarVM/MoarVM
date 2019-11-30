MVMString * MVM_string_latin1_decode(MVMThreadContext *tc, const MVMObject *result_type, char *latin1, size_t bytes);
MVM_PUBLIC MVMuint32 MVM_string_latin1_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMuint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
char * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
char * MVM_string_latin1_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint32 translate_newlines);
