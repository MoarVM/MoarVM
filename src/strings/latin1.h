MVMString * MVM_string_latin1_decode(MVMThreadContext *tc, MVMObject *result_type, char *latin1, size_t bytes);
MVM_PUBLIC void MVM_string_latin1_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, MVMint32 *stopper_chars, MVMint32 *stopper_sep);
char * MVM_string_latin1_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length);
char * MVM_string_latin1_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
