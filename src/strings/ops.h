/* Encoding types and encoding validity check. */
#define MVM_encoding_type_MIN           1
#define MVM_encoding_type_utf8          1
#define MVM_encoding_type_ascii         2
#define MVM_encoding_type_latin1        3
#define MVM_encoding_type_utf16         4
#define MVM_encoding_type_windows1252   5
#define MVM_encoding_type_utf8_c8       6
#define MVM_encoding_type_MAX           6
#define ENCODING_VALID(enc) \
    (((enc) >= MVM_encoding_type_MIN && (enc) <= MVM_encoding_type_MAX) \
    || (MVM_exception_throw_adhoc(tc, "invalid encoding type flag: %d", (enc)),1))

/* Character class constants (map to nqp::const::CCLASS_* values). */
#define MVM_CCLASS_ANY          65535
#define MVM_CCLASS_UPPERCASE    1
#define MVM_CCLASS_LOWERCASE    2
#define MVM_CCLASS_ALPHABETIC   4
#define MVM_CCLASS_NUMERIC      8
#define MVM_CCLASS_HEXADECIMAL  16
#define MVM_CCLASS_WHITESPACE   32
#define MVM_CCLASS_PRINTING     64
#define MVM_CCLASS_BLANK        256
#define MVM_CCLASS_CONTROL      512
#define MVM_CCLASS_PUNCTUATION  1024
#define MVM_CCLASS_ALPHANUMERIC 2048
#define MVM_CCLASS_NEWLINE      4096
#define MVM_CCLASS_WORD         8192

/* Checks a string is not null or non-concrete and throws if so. */
MVM_STATIC_INLINE void MVM_string_check_arg(MVMThreadContext *tc, const MVMString *s, const char *operation) {
    if (!s || !IS_CONCRETE(s))
        MVM_exception_throw_adhoc(tc, "%s requires a concrete string, but got %s",
            operation, s ? "a type object" : "null");
}

MVM_STATIC_INLINE MVMuint32 MVM_string_graphs(MVMThreadContext *tc, MVMString *s) {
    MVM_string_check_arg(tc, s, "chars");
    return s->body.num_graphs;
}
MVM_STATIC_INLINE MVMuint32 MVM_string_graphs_nocheck(MVMThreadContext *tc, MVMString *s) {
    return s->body.num_graphs;
}
MVM_STATIC_INLINE MVMuint32 MVM_string_codes(MVMThreadContext *tc, MVMString *s) {
    MVMCodepointIter ci;
    MVMint64 codes = 0;
    MVM_string_check_arg(tc, s, "codes");
    if (MVM_string_graphs_nocheck(tc, s) == 0)
        return 0;
    MVM_string_ci_init(tc, &ci, s, 0);
    while(MVM_string_ci_has_more(tc, &ci)) {
        MVM_string_ci_get_codepoint(tc, &ci);
        codes++;
    }
    return codes;
}

MVMGrapheme32 MVM_string_get_grapheme_at_nocheck(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_equal(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMint64 MVM_string_index(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMint64 MVM_string_index_ignore_case(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMint64 MVM_string_index_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 start);
MVMint64 MVM_string_index_ignore_case_ignore_mark(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMint64 MVM_string_index_from_end(MVMThreadContext *tc, MVMString *haystack, MVMString *needle, MVMint64 start);
MVMString * MVM_string_concatenate(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_repeat(MVMThreadContext *tc, MVMString *a, MVMint64 count);
MVMString * MVM_string_substring(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length);
MVMString * MVM_string_replace(MVMThreadContext *tc, MVMString *a, MVMint64 start, MVMint64 length, MVMString *replacement);
void MVM_string_say(MVMThreadContext *tc, MVMString *a);
void MVM_string_print(MVMThreadContext *tc, MVMString *a);
MVMint64 MVM_string_equal_at(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_equal_at_ignore_case(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMint64 MVM_string_equal_at_ignore_mark(MVMThreadContext *tc, MVMString *Haystack, MVMString *needle, MVMint64 H_offset);
MVMint64 MVM_string_equal_at_ignore_case_ignore_mark(MVMThreadContext *tc, MVMString *a, MVMString *b, MVMint64 offset);
MVMGrapheme32 MVM_string_ord_basechar_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset);
MVMGrapheme32 MVM_string_ord_at(MVMThreadContext *tc, MVMString *s, MVMint64 offset);
MVMint64 MVM_string_have_at(MVMThreadContext *tc, MVMString *a, MVMint64 starta, MVMint64 length, MVMString *b, MVMint64 startb);
MVMint64 MVM_string_get_grapheme_at(MVMThreadContext *tc, MVMString *a, MVMint64 index);
MVMint64 MVM_string_index_of_grapheme(MVMThreadContext *tc, MVMString *a, MVMGrapheme32 codepoint);
MVMString * MVM_string_uc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_lc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_tc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_fc(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_decode(MVMThreadContext *tc, const MVMObject *type_object, char *Cbuf, MVMint64 byte_length, MVMint64 encoding_flag);
char * MVM_string_encode(MVMThreadContext *tc, MVMString *s, MVMint64 start, MVMint64 length, MVMuint64 *output_size, MVMint64 encoding_flag, MVMString *replacement, MVMint32 translate_newlines);
MVMObject * MVM_string_encode_to_buf(MVMThreadContext *tc, MVMString *s, MVMString *enc_name, MVMObject *buf, MVMString *replacement);
MVMString * MVM_string_decode_from_buf(MVMThreadContext *tc, MVMObject *buf, MVMString *enc_name);
MVMObject * MVM_string_split(MVMThreadContext *tc, MVMString *separator, MVMString *input);
MVMString * MVM_string_join(MVMThreadContext *tc, MVMString *separator, MVMObject *input);
MVMint64 MVM_string_char_at_in_string(MVMThreadContext *tc, MVMString *a, MVMint64 offset, MVMString *b);
MVMint64 MVM_string_offset_has_unicode_property_value(MVMThreadContext *tc, MVMString *s, MVMint64 offset, MVMint64 property_code, MVMint64 property_value_code);
MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMint64 grapheme, MVMint64 property_code, MVMint64 property_value_code);
MVMString * MVM_unicode_codepoint_get_property_str(MVMThreadContext *tc, MVMint64 grapheme, MVMint64 property_code);
const char * MVM_unicode_codepoint_get_property_cstr(MVMThreadContext *tc, MVMint64 grapheme, MVMint64 property_code);
MVMint64 MVM_unicode_codepoint_get_property_int(MVMThreadContext *tc, MVMint64 grapheme, MVMint64 property_code);
MVMint64 MVM_unicode_codepoint_get_property_bool(MVMThreadContext *tc, MVMint64 grapheme, MVMint64 property_code);
MVMString * MVM_unicode_get_name(MVMThreadContext *tc, MVMint64 grapheme);
MVMString * MVM_string_indexing_optimized(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_escape(MVMThreadContext *tc, MVMString *s);
MVMString * MVM_string_flip(MVMThreadContext *tc, MVMString *s);
MVMint64 MVM_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_bitand(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_bitor(MVMThreadContext *tc, MVMString *a, MVMString *b);
MVMString * MVM_string_bitxor(MVMThreadContext *tc, MVMString *a, MVMString *b);
void MVM_string_cclass_init(MVMThreadContext *tc);
MVMint64 MVM_string_is_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset);
MVMint64 MVM_string_find_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count);
MVMint64 MVM_string_find_not_cclass(MVMThreadContext *tc, MVMint64 cclass, MVMString *s, MVMint64 offset, MVMint64 count);
MVMuint8 MVM_string_find_encoding(MVMThreadContext *tc, MVMString *name);
MVMString * MVM_string_chr(MVMThreadContext *tc, MVMint64 cp);
void MVM_string_compute_hash_code(MVMThreadContext *tc, MVMString *s);
/* If MVM_DEBUG_NFG is 1, calls to NFG_CHECK will re_nfg the given string
 * and compare num_graphs before and after the normalization.
 * If it is different debug information will be printed out.
#define MVM_DEBUG_NFG 1 */
/* MVM_DEBUG_NFG_STRICT does as above but does not only rely on num_graphs. It
 * always checks every grapheme manually. Slower. (requires MVM_DEBUG_NFG)
#define MVM_DEBUG_NFG_STRICT 0 */
#if MVM_DEBUG_NFG
void NFG_check (MVMThreadContext *tc, MVMString *orig, char *varname);
void NFG_check_concat (MVMThreadContext *tc, MVMString *result, MVMString *a, MVMString *b, char *varname);
#define NFG_CHECK(tc, s, varname)              NFG_check(tc, s, varname);
#define NFG_CHECK_CONCAT(tc, s, a, b, varname) NFG_check_concat(tc, s, a, b, varname);
#else
#define NFG_CHECK(tc, s, varname)
#define NFG_CHECK_CONCAT(tc, s, a, b, varname)
#endif
