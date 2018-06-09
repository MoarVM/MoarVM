MVMint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name);
MVMint64 MVM_unicode_has_property_value(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code, MVMint64 property_value_code);
MVMuint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMCodepoint codepoint, MVMint32 case_, const MVMCodepoint **result);
MVMint64 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name);
MVMint64 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name);
MVMint32 MVM_unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, size_t cname_length);
MVMCodepoint MVM_unicode_find_primary_composite(MVMThreadContext *tc, MVMCodepoint l, MVMCodepoint c);

#define MVM_unicode_case_change_type_upper 0
#define MVM_unicode_case_change_type_lower 1
#define MVM_unicode_case_change_type_title 2
#define MVM_unicode_case_change_type_fold  3

struct MVMUnicodeNameRegistry {
    char *name;
    MVMGrapheme32 codepoint;
    UT_hash_handle hash_handle;
};
struct MVMUnicodeGraphemeNameRegistry {
    char *name;
    MVMint32 structindex;
    UT_hash_handle hash_handle;
};

void MVM_unicode_init(MVMThreadContext *tc);
void MVM_unicode_release(MVMThreadContext *tc);
