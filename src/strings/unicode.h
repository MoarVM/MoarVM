MVMint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name);
MVMint64 MVM_unicode_has_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code, MVMint64 property_value_code);
MVMint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMint32 codepoint, MVMint32 case_);
MVMint64 MVM_unicode_get_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code)

typedef struct _MVMUnicodeNameHashEntry {
    char *name;
    MVMint32 codepoint;
    UT_hash_handle hash_handle;
} MVMUnicodeNameHashEntry;
