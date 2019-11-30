/* Bitmap controling whether we throw on codepoints which don't have mappings (yet still
 * fit in one byte). If use loose we we pass through the codepoint unchanged if it fits
 * in one byte. */
#define MVM_ENCODING_PERMISSIVE 1
#define MVM_ENCODING_CONFIG_STRICT(config) (!(config & MVM_ENCODING_PERMISSIVE))
#define MVM_ENCODING_CONFIG_PERMISSIVE(config) (config & MVM_ENCODING_PERMISSIVE)
MVMString * MVM_string_windows1252_decode(MVMThreadContext *tc, const MVMObject *result_type, char *windows1252, size_t bytes);
MVM_PUBLIC MVMuint32 MVM_string_windows1252_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMuint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
char * MVM_string_windows1252_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
char * MVM_string_windows1252_encode_substr_config(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines, MVMint64 bitmap);
char * MVM_string_windows1251_encode_substr_config(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines, MVMint64 bitmap);
MVMString * MVM_string_windows1251_decode(MVMThreadContext *tc, const MVMObject *result_type, char *windows1252, size_t bytes);
MVM_PUBLIC MVMuint32 MVM_string_windows1251_decodestream(MVMThreadContext *tc, MVMDecodeStream *ds, const MVMuint32 *stopper_chars, MVMDecodeStreamSeparators *seps);
char * MVM_string_windows1251_encode_substr(MVMThreadContext *tc, MVMString *str, MVMuint64 *output_size, MVMint64 start, MVMint64 length, MVMString *replacement, MVMint32 translate_newlines);
MVMString * MVM_string_windows1252_decode_config(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes,
        MVMString *replacement, MVMint64 bitmap);
MVMString * MVM_string_windows1251_decode_config(MVMThreadContext *tc,
        const MVMObject *result_type, char *windows125X_c, size_t bytes,
        MVMString *replacement, MVMint64 bitmap);
