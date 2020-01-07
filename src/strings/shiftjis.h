char * MVM_string_shiftjis_encode_substr(MVMThreadContext *tc, MVMString *str,
        MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement,
        MVMint32 translate_newlines, MVMint64 config);
MVMuint32 MVM_string_shiftjis_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds,
                                         const MVMuint32 *stopper_chars,
                                         MVMDecodeStreamSeparators *seps);
MVMString * MVM_string_shiftjis_decode(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t num_bytes,
        MVMString *replacement, MVMint64 config);
