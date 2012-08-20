
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

MVMint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMint32 codepoint, MVMint32 case_) {
    MVMint32 changes_index = MVM_unicode_get_property_value(tc,
        codepoint, MVM_UNICODE_PROPERTY_CASE_CHANGE_INDEX);
    
    return changes_index ? case_changes[changes_index][case_] : codepoint;
}

/* XXX make all the statics members of the global MVM instance instead? */
static MVMUnicodeNameHashEntry *property_codes_by_names_aliases;

void generate_property_codes_by_names_aliases(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_property_keypairs;
    
    while (num_names--) {
        MVMUnicodeNameHashEntry *entry = malloc(sizeof(MVMUnicodeNameHashEntry));
        entry->name = (char *)unicode_property_keypairs[num_names].name;
        entry->codepoint = unicode_property_keypairs[num_names].value;
        HASH_ADD_KEYPTR(hash_handle, property_codes_by_names_aliases,
            entry->name, strlen(entry->name), entry);
    }
}

MVMint32 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;
    if (!property_codes_by_names_aliases) {
        generate_property_codes_by_names_aliases(tc);
    }
    HASH_FIND(hash_handle, property_codes_by_names_aliases, cname, strlen(cname), result);
    free(cname); /* not really codepoint, really just an index */
    return result ? result->codepoint : 0;
}
