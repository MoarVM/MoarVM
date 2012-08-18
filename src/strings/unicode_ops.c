
/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;
    if (!codepoints_by_name) {
        generate_codepoints_by_name(tc);
    }
    HASH_FIND(hash_handle, codepoints_by_name, cname, strlen(cname), result);
    free(cname);
    return result ? result->codepoint : -1;
}

MVMint64 MVM_unicode_has_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    return (MVMint64)MVM_unicode_get_property_value(tc,
        codepoint, property_code) == property_value_code ? 1 : 0;
}

MVMint64 MVM_unicode_get_property_value(MVMThreadContext *tc, MVMint32 codepoint, MVMint64 property_code) {
    return (MVMint64)MVM_unicode_get_property_value(tc, codepoint, property_code);
}

MVMint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMint32 codepoint, MVMint32 case_) {
    MVMint32 changes_index = MVM_unicode_get_property_value(tc,
        codepoint, MVM_UNICODE_PROPERTY_CASE_CHANGE_INDEX);
    
    return changes_index ? case_changes[changes_index][case_] : codepoint;
}

