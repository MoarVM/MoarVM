
/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMCodepoint32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    unsigned char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;
    if (!codepoints_by_name) {
        generate_codepoints_by_name(tc);
    }
    HASH_FIND(hash_handle, codepoints_by_name, cname, strlen((const char *)cname), result);
    free(cname);
    return result ? result->codepoint : -1;
}

MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMCodepoint32 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    return (MVMint64)MVM_unicode_get_property_value(tc,
        codepoint, property_code) == property_value_code ? 1 : 0;
}

MVMCodepoint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMCodepoint32 codepoint, MVMint32 case_) {
    MVMint32 changes_index = MVM_unicode_get_property_value(tc,
        codepoint, MVM_UNICODE_PROPERTY_CASE_CHANGE_INDEX);

    if (changes_index) {
        MVMCodepoint32 result = case_changes[changes_index][case_];
        if (result == 0) return codepoint;
        return result;
    }
    return codepoint;
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
    unsigned char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;
    if (!property_codes_by_names_aliases) {
        generate_property_codes_by_names_aliases(tc);
    }
    HASH_FIND(hash_handle, property_codes_by_names_aliases, cname, strlen((const char *)cname), result);
    free(cname); /* not really codepoint, really just an index */
    return result ? result->codepoint : 0;
}

static void generate_unicode_property_values_hashes(MVMThreadContext *tc) {
    /* XXX make this synchronized, I guess... */
    MVMUnicodeNameHashEntry **hash_array = calloc(sizeof(MVMUnicodeNameHashEntry *), MVMNUMPROPERTYCODES);
    MVMuint32 index = 0;
    MVMUnicodeNameHashEntry *entry = NULL, *binaries = NULL;
    for ( ; index < num_unicode_property_value_keypairs; index++) {
        MVMint32 property_code = unicode_property_value_keypairs[index].value >> 24;
        entry = malloc(sizeof(MVMUnicodeNameHashEntry));
        entry->name = (char *)unicode_property_value_keypairs[index].name;
        entry->codepoint = unicode_property_value_keypairs[index].value & 0xFFFFFF;
        HASH_ADD_KEYPTR(hash_handle, hash_array[property_code],
            entry->name, strlen(entry->name), entry);
    }
    for (index = 0; index < MVMNUMPROPERTYCODES; index++) {
        if (!hash_array[index]) {
            if (!binaries) {
                MVMUnicodeNamedValue yes[8] = { {"T",1}, {"Y",1},
                    {"Yes",1}, {"yes",1}, {"True",1}, {"true",1}, {"t",1}, {"y",1} };
                MVMUnicodeNamedValue no [8] = { {"F",0}, {"N",0},
                    {"No",0}, {"no",0}, {"False",0}, {"false",0}, {"f",0}, {"n",0} };
                MVMuint8 i;
                for (i = 0; i < 8; i++) {
                    entry = malloc(sizeof(MVMUnicodeNameHashEntry));
                    entry->name = (char *)yes[i].name;
                    entry->codepoint = yes[i].value;
                    HASH_ADD_KEYPTR(hash_handle, binaries, yes[i].name, strlen(yes[i].name), entry);
                }
                for (i = 0; i < 8; i++) {
                    entry = malloc(sizeof(MVMUnicodeNameHashEntry));
                    entry->name = (char *)no[i].name;
                    entry->codepoint = no[i].value;
                    HASH_ADD_KEYPTR(hash_handle, binaries, no[i].name, strlen(no[i].name), entry);
                }
            }
            hash_array[index] = binaries;
        }
    }
    unicode_property_values_hashes = hash_array;
}

MVMint32 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name) {
    MVMuint64 size;
    unsigned char *cname = MVM_string_ascii_encode(tc, name, &size);
    MVMUnicodeNameHashEntry *result;

    if (property_code < 0 || property_code >= MVMNUMPROPERTYCODES)
        return 0;

    if (!unicode_property_values_hashes) {
        generate_unicode_property_values_hashes(tc);
    }
    HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], cname, strlen((const char *)cname), result);
    free(cname); /* not really codepoint, really just an index */
    return result ? result->codepoint : 0;
}
