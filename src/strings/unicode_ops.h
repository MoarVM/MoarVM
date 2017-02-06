MVMint64 MVM_unicode_string_compare(MVMThreadContext *tc, MVMString *a, MVMString *b,
    MVMint64 collation_mode, MVMint64 lang_mode, MVMint64 country_mode);

MVMString * MVM_unicode_string_from_name(MVMThreadContext *tc, MVMString *name);
