MVMint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name);
MVMint64 MVM_unicode_has_property_value(MVMThreadContext *tc, MVMCodepoint32 codepoint, MVMint64 property_code, MVMint64 property_value_code);
MVMCodepoint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMCodepoint32 codepoint, MVMint32 case_);
MVMint32 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name);
MVMint32 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name);
#define MVM_unicode_case_change_type_upper 0
#define MVM_unicode_case_change_type_lower 1
#define MVM_unicode_case_change_type_title 2

typedef struct _MVMUnicodeNameHashEntry {
    char *name;
    MVMCodepoint32 codepoint;
    UT_hash_handle hash_handle;
} MVMUnicodeNameHashEntry;
