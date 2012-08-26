#define MVM_encoding_type_utf8 1
#define MVM_encoding_type_ascii 2
#define MVM_encoding_type_latin1 3
#define ENCODING_VALID(enc) (((enc) >= MVM_encoding_type_utf8 && (enc) <= MVM_encoding_type_latin1) \
                            || (MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", (enc)),1))


MVMCodepoint32 MVM_string_get_codepoint_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMStrandIndex MVM_string_rope_strands_size(MVMThreadContext *tc, MVMStringBody *body);
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 start);
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count);
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length);
void MVM_string_say(MVMThreadContext *tc, MVMString *a);
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a, MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb);
MVMint64 MVM_string_get_codepoint_at(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_index_of_codepoint(MVMThreadContext *tc, MVMString *a, MVMint64 codepoint);
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_decode_C_buffer_to_string(MVMThreadContext *tc, MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag);
char * MVM_encode_string_to_C_buffer(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag);
MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *input, MVMObject *type_object, MVMString *separator);
MVMString * MVM_string_join(MVMThreadContext *tc, MVMObject *input, MVMString *separator);
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b);
void MVM_string_flatten(MVMThreadContext *tc, MVMString *s);