MVMString * MVM_string_wide_decode(MVMThreadContext *tc, const MVMwchar *wstr, size_t bytes);
MVMwchar  * MVM_string_wide_encode(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size);
