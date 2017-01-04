/* Compares two strings, based on Unicode Collation Algorithm
 *    0   The strings are identical, includes codepoints
 * -1/1   We used the primary collation values to decide the result
 * -2/2   We used the secondary, meaning the primary weights were equal
 * -3/3   We used the tetriary, meaning the primary and also the secondary
          weights (if we used that option, which is the default) were equal.
 *   -4/4 We used codepoints to decide because primary, secondary and/or tetriary
          were equal (depending on if secondary and tetriary were requested).
          If the codepoints are all the same, we will decide based on length.
 * -10/10 The collation algorithm was not able to be applied and so they
          were compared based on codepoints. If codepoints were equal they were
          compared by length. */
MVMint32 MVM_unicode_collation_primary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_PRIMARY);
}
MVMint32 MVM_unicode_collation_secondary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_SECONDARY);
}
MVMint32 MVM_unicode_collation_tertiary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_TERTIARY);
}
MVMint64 MVM_unicode_string_compare
        (MVMThreadContext *tc, MVMString *a, MVMString *b,
         MVMint32 collation_mode, MVMint32 lang_mode, MVMint32 country_mode) {
    MVMStringIndex alen, blen, i, scanlen;
    /* Iteration variables */
    MVMGrapheme32 ai;
    MVMGrapheme32 bi;
    /* Collation order numbers */
    MVMint32 ai_coll_val;
    MVMint32 bi_coll_val;
    MVM_string_check_arg(tc, a, "compare");
    MVM_string_check_arg(tc, b, "compare");
    /* Simple cases when one or both are zero length. */
    alen = MVM_string_graphs(tc, a);
    blen = MVM_string_graphs(tc, b);
    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;

    /* Otherwise, need to scan them. */
    scanlen = alen > blen ? blen : alen;
    for (i = 0; i < scanlen; i++) {
        ai = MVM_string_get_grapheme_at_nocheck(tc, a, i);
        bi = MVM_string_get_grapheme_at_nocheck(tc, b, i);
        /* If they are the same grapheme */
        if (ai != bi) {
            /* only try and get a property if the value it is isn't a synthetic
             * grapheme. we should change from get_grapheme to something to get the
             * NFC form */
            if ( ai >= 0 || bi >= 0 ) {
                /* Get the primary collation value for the grapheme */
                ai_coll_val = MVM_unicode_collation_primary(tc, ai);
                bi_coll_val = MVM_unicode_collation_primary(tc, bi);
                /* If we don't find a collation value,
                   we should compare by codepoint */
                /* Eventually we should try and catch codepoints that don't
                   have a collation value. We would then need to decompose it and
                   apply weighting based on their decomposed values. */
                if (ai_coll_val == 0 || bi_coll_val == 0) {
                    /* return -10 or 10 to indicate we didn't use the collation
                       algorithm */
                    return ai < bi ? -10 :
                           ai > bi ?  10 :
                                       0 ;
                }

                /* If both have primary collation values ( they are not 0 ) */
                if ( (ai_coll_val != 0 && bi_coll_val != 0) && (ai_coll_val != bi_coll_val) ) {
                    return ai_coll_val < bi_coll_val ? -1 : 1;
                }
                /* If both have the same primary collation values */
                ai_coll_val += MVM_unicode_collation_secondary(tc, ai);
                bi_coll_val += MVM_unicode_collation_secondary(tc, bi);
                if ( (ai_coll_val != 0 && bi_coll_val != 0) && (ai_coll_val != bi_coll_val) ) {
                    return ai_coll_val < bi_coll_val ? -2 : 2;
                }
                /* If both have the same tertiary collation values */
                ai_coll_val = MVM_unicode_collation_tertiary(tc, ai);
                bi_coll_val = MVM_unicode_collation_tertiary(tc, bi);
                if ( (ai_coll_val != 0 && bi_coll_val != 0) && (ai_coll_val != bi_coll_val) ) {
                    return ai_coll_val < bi_coll_val ? -3 : 3;
                }
                /* All the collation values were equal. Check codepoints */
                return ai < bi ? -4 :
                       ai > bi ?  4 :
                                  0 ;

            }
            /* For now, if it's a synthetic codepoint just compare by codepoint. */
            else {
                return ai < bi ? -10 :
                       ai > bi ?  10 :
                                   1 ;
            }
        }
    }

    /* All shared chars equal, so go on length. */
    return alen < blen ? -1 :
           alen > blen ?  1 :
                          0 ;
}

/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMGrapheme32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
    MVMUnicodeNameRegistry *result;
    if (!codepoints_by_name) {
        generate_codepoints_by_name(tc);
    }
    HASH_FIND(hash_handle, codepoints_by_name, cname, strlen((const char *)cname), result);
    MVM_free(cname);
    return result ? result->codepoint : -1;
}

MVMString * MVM_unicode_get_name(MVMThreadContext *tc, MVMint64 codepoint) {
    const char *name;

    /* Catch out-of-bounds code points. */
    if (codepoint < 0) {
        name = "<illegal>";
    }
    else if (codepoint > 0x10ffff) {
        name = "<unassigned>";
    }

    /* Look up name. */
    else {
        MVMuint32 codepoint_row = MVM_codepoint_to_row_index(tc, codepoint);
        if (codepoint_row != -1) {
            name = codepoint_names[codepoint_row];
            if (!name) {
                while (codepoint_row && !codepoint_names[codepoint_row])
                    codepoint_row--;
                name = codepoint_names[codepoint_row];
                if (!name || name[0] != '<')
                    name = "<reserved>";
            }
        }
        else {
            name = "<illegal>";
        }
    }

    return MVM_string_ascii_decode(tc, tc->instance->VMString, name, strlen(name));
}

MVMString * MVM_unicode_codepoint_get_property_str(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    const char * const str = MVM_unicode_get_property_str(tc, codepoint, property_code);

    if (!str)
        return tc->instance->str_consts.empty;

    return MVM_string_ascii_decode(tc, tc->instance->VMString, str, strlen(str));
}

const char * MVM_unicode_codepoint_get_property_cstr(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    return MVM_unicode_get_property_str(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_int(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_bool(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code) != 0;
}

MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMGrapheme32 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc,
        codepoint, property_code) == property_value_code ? 1 : 0;
}

/* Looks if there is a case change for the provided codepoint. Since a case
 * change may produce multiple codepoints occasionally, then we return 0 if
 * the case change is a no-op, and otherwise the number of codepoints. The
 * codepoints argument will be set to a pointer to a buffer where those code
 * points can be read from. The caller must not mutate the buffer, nor free
 * it. */
MVMuint32 MVM_unicode_get_case_change(MVMThreadContext *tc, MVMCodepoint codepoint, MVMint32 case_,
                                      const MVMCodepoint **result) {
    if (case_ == MVM_unicode_case_change_type_fold) {
        MVMint32 folding_index = MVM_unicode_get_property_int(tc,
            codepoint, MVM_UNICODE_PROPERTY_CASE_FOLDING);
        if (folding_index) {
            MVMint32 is_simple = MVM_unicode_get_property_int(tc,
                codepoint, MVM_UNICODE_PROPERTY_CASE_FOLDING_SIMPLE);
            if (is_simple) {
                *result = &(CaseFolding_simple_table[folding_index]);
                return 1;
            }
            else {
                MVMint32 i = 3;
                while (i > 0 && CaseFolding_grows_table[folding_index][i - 1] == 0)
                    i--;
                *result = &(CaseFolding_grows_table[folding_index][0]);
                return i;
            }
        }
    }
    else {
        MVMint32 special_casing_index = MVM_unicode_get_property_int(tc,
            codepoint, MVM_UNICODE_PROPERTY_SPECIAL_CASING);
        if (special_casing_index) {
            MVMint32 i = 3;
                while (i > 0 && SpecialCasing_table[special_casing_index][case_][i - 1] == 0)
                    i--;
                *result = SpecialCasing_table[special_casing_index][case_];
                return i;
        }
        else {
            MVMint32 changes_index = MVM_unicode_get_property_int(tc,
                codepoint, MVM_UNICODE_PROPERTY_CASE_CHANGE_INDEX);
            if (changes_index) {
                const MVMCodepoint *found = &(case_changes[changes_index][case_]);
                if (*found != 0) {
                    *result = found;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* XXX make all the statics members of the global MVM instance instead? */
static MVMUnicodeNameRegistry *property_codes_by_names_aliases;

static void generate_property_codes_by_names_aliases(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_property_keypairs;

    while (num_names--) {
        MVMUnicodeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
        entry->name = (char *)unicode_property_keypairs[num_names].name;
        entry->codepoint = unicode_property_keypairs[num_names].value;
        HASH_ADD_KEYPTR(hash_handle, property_codes_by_names_aliases,
            entry->name, strlen(entry->name), entry);
    }
}

MVMint32 MVM_unicode_name_to_property_code(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
    MVMUnicodeNameRegistry *result;
    if (!property_codes_by_names_aliases) {
        generate_property_codes_by_names_aliases(tc);
    }
    HASH_FIND(hash_handle, property_codes_by_names_aliases, cname, strlen((const char *)cname), result);
    MVM_free(cname); /* not really codepoint, really just an index */
    return result ? result->codepoint : 0;
}

static void generate_unicode_property_values_hashes(MVMThreadContext *tc) {
    MVMUnicodeNameRegistry **hash_array = MVM_calloc(sizeof(MVMUnicodeNameRegistry *), MVM_NUM_PROPERTY_CODES);
    MVMuint32 index = 0;
    MVMUnicodeNameRegistry *entry = NULL, *binaries = NULL;
    for ( ; index < num_unicode_property_value_keypairs; index++) {
        MVMint32 property_code = unicode_property_value_keypairs[index].value >> 24;
        entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
        entry->name = (char *)unicode_property_value_keypairs[index].name;
        entry->codepoint = unicode_property_value_keypairs[index].value & 0xFFFFFF;
        HASH_ADD_KEYPTR(hash_handle, hash_array[property_code],
            entry->name, strlen(entry->name), entry);
    }
    for (index = 0; index < MVM_NUM_PROPERTY_CODES; index++) {
        if (!hash_array[index]) {
            if (!binaries) {
                MVMUnicodeNamedValue yes[8] = { {"T",1}, {"Y",1},
                    {"Yes",1}, {"yes",1}, {"True",1}, {"true",1}, {"t",1}, {"y",1} };
                MVMUnicodeNamedValue no [8] = { {"F",0}, {"N",0},
                    {"No",0}, {"no",0}, {"False",0}, {"false",0}, {"f",0}, {"n",0} };
                MVMuint8 i;
                for (i = 0; i < 8; i++) {
                    entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
                    entry->name = (char *)yes[i].name;
                    entry->codepoint = yes[i].value;
                    HASH_ADD_KEYPTR(hash_handle, binaries, yes[i].name, strlen(yes[i].name), entry);
                }
                for (i = 0; i < 8; i++) {
                    entry = MVM_malloc(sizeof(MVMUnicodeNameRegistry));
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
    if (property_code <= 0 || property_code >= MVM_NUM_PROPERTY_CODES) {
        return 0;
    }
    else {
        MVMuint64 size;
        char *cname = MVM_string_ascii_encode(tc, name, &size, 0);
        MVMUnicodeNameRegistry *result;

        HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], cname, strlen((const char *)cname), result);
        MVM_free(cname); /* not really codepoint, really just an index */
        return result ? result->codepoint : 0;
    }
}

MVMint32 MVM_unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, size_t cname_length) {
    if (property_code <= 0 || property_code >= MVM_NUM_PROPERTY_CODES) {
        return 0;
    }
    else {
        MVMuint64 size;
        MVMUnicodeNameRegistry *result;

        HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], cname, cname_length, result);
        return result ? result->codepoint : 0;
    }
}

/* Look up the primary composite for a pair of codepoints, if it exists.
 * Returns 0 if not. */
MVMCodepoint MVM_unicode_find_primary_composite(MVMThreadContext *tc, MVMCodepoint l, MVMCodepoint c) {
    MVMint32 lower = l & 0xFF;
    MVMint32 upper = (l >> 8) & 0xFF;
    MVMint32 plane = (l >> 16) & 0xF;
    const MVMint32 *pcs  = comp_p[plane][upper][lower];
    if (pcs) {
        MVMint32 entries = pcs[0];
        MVMint32 i;
        for (i = 1; i < entries; i += 2)
            if (pcs[i] == c)
                return pcs[i + 1];
    }
    return 0;
}

static uv_mutex_t property_hash_count_mutex;
static int property_hash_count = 0;
static uv_once_t property_hash_count_guard = UV_ONCE_INIT;

static void setup_property_mutex(void)
{
    uv_mutex_init(&property_hash_count_mutex);
}

void MVM_unicode_init(MVMThreadContext *tc)
{
    uv_once(&property_hash_count_guard, setup_property_mutex);

    uv_mutex_lock(&property_hash_count_mutex);
    if (property_hash_count == 0) {
        generate_unicode_property_values_hashes(tc);
    }
    property_hash_count++;
    uv_mutex_unlock(&property_hash_count_mutex);
}

void MVM_unicode_release(MVMThreadContext *tc)
{
    uv_mutex_lock(&property_hash_count_mutex);
    property_hash_count--;
    if (property_hash_count == 0) {
        int i;

        for (i = 0; i < MVM_NUM_PROPERTY_CODES; i++) {
            MVMUnicodeNameRegistry *entry;
            MVMUnicodeNameRegistry *tmp;
            unsigned bucket_tmp;
            int j;

            if (!unicode_property_values_hashes[i]) {
                continue;
            }

            for(j = i + 1; j < MVM_NUM_PROPERTY_CODES; j++) {
                if (unicode_property_values_hashes[i] == unicode_property_values_hashes[j]) {
                    unicode_property_values_hashes[j] = NULL;
                }
            }

            HASH_ITER(hash_handle, unicode_property_values_hashes[i], entry, tmp, bucket_tmp) {
                HASH_DELETE(hash_handle, unicode_property_values_hashes[i], entry);
                MVM_free(entry);
            }
            assert(!unicode_property_values_hashes[i]);
        }

        MVM_free(unicode_property_values_hashes);

        unicode_property_values_hashes = NULL;
    }
    uv_mutex_unlock(&property_hash_count_mutex);
}
