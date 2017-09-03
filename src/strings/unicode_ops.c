/* Compares two strings, using the Unicode Collation Algorithm
 * Return values:
 *    0   The strings are identical for the collation levels requested
 * -1/1   String a is less than string b/String a is greater than string b
 *
 * `collation_mode` acts like a bitfield. Each of primary, secondary and tertiary
 * collation levels can be either: disabled, enabled, reversed.
 * In the table below, where + designates sorting normal direction and
 * - indicates reversed sorting for that collation level.
 *
 * Collation level | bitfield value
 *        Primary+ | 1
 *        Primary- | 2
 *      Secondary+ | 4
 *      Secondary- | 8
 *       Tertiary+ | 16
 *       Tertiary- | 32
 */

MVMint32 MVM_unicode_collation_primary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_PRIMARY);
}
MVMint32 MVM_unicode_collation_secondary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_SECONDARY);
}
MVMint32 MVM_unicode_collation_tertiary (MVMThreadContext *tc, MVMint32 codepoint) {
     return MVM_unicode_codepoint_get_property_int(tc, codepoint, MVM_UNICODE_PROPERTY_MVM_COLLATION_TERTIARY);
}
/* coll_val is where the collation value will be placed. In the case the
 * collation order is reversed for that level, it will be placed in coll_val_rev */
#define collation_adjust(tc, coll_val, coll_val_rev, collation_mode, cp) {\
    if (collation_mode & 1)\
        coll_val[0]     += MVM_unicode_collation_primary(tc, cp);\
    if (collation_mode & 2)\
        coll_val_rev[0] += MVM_unicode_collation_primary(tc, cp);\
    if (collation_mode & 4)\
        coll_val[1]     += MVM_unicode_collation_secondary(tc, cp);\
    if (collation_mode & 8)\
        coll_val_rev[1] += MVM_unicode_collation_secondary(tc, cp);\
    if (collation_mode & 16)\
        coll_val[2]     += MVM_unicode_collation_tertiary(tc, cp);\
    if (collation_mode & 32)\
        coll_val_rev[2] += MVM_unicode_collation_tertiary(tc, cp);\
}
/* MVM_unicode_string_compare supports synthetic graphemes but in case we have
 * a codepoint without any collation value, we do not yet decompose it and
 * then use the decomposed codepoint's weights. */
MVMint64 MVM_unicode_string_compare
        (MVMThreadContext *tc, MVMString *a, MVMString *b,
         MVMint64 collation_mode, MVMint64 lang_mode, MVMint64 country_mode) {
    MVMStringIndex alen, blen;
    /* Iteration variables */
    MVMGraphemeIter a_gi, b_gi;
    MVMGraphemeIter *s_has_more_gi;
    MVMGrapheme32 ai, bi;
    /* Collation order numbers */
    MVMuint32 ai_coll_val[3] = {0,0,0};
    MVMuint32 bi_coll_val[3] = {0,0,0};
    MVM_string_check_arg(tc, a, "compare");
    MVM_string_check_arg(tc, b, "compare");
    /* Simple cases when one or both are zero length. */
    alen = MVM_string_graphs_nocheck(tc, a);
    blen = MVM_string_graphs_nocheck(tc, b);
    if (alen == 0)
        return blen == 0 ? 0 : -1;
    if (blen == 0)
        return 1;
    /* We only check whether the shorter string has more each iteration
     * so find which string is longer */
    s_has_more_gi = alen > blen ? &b_gi : &a_gi;
    /* Initialize a grapheme iterator */
    MVM_string_gi_init(tc, &a_gi, a);
    MVM_string_gi_init(tc, &b_gi, b);

    /* Otherwise, need to iterate by grapheme */
    while (MVM_string_gi_has_more(tc, s_has_more_gi)) {
        ai = MVM_string_gi_get_grapheme(tc, &a_gi);
        bi = MVM_string_gi_get_grapheme(tc, &b_gi);
        /* Only need to do this if they're not the same grapheme */
        if (ai != bi) {
            int rtrn, i = 0;
            /* If it's less than zero we have a synthetic codepoint */
            if (ai < 0) {
                MVMCodepointIter a_ci;
                MVM_string_grapheme_ci_init(tc, &a_ci, ai);
                while (MVM_string_grapheme_ci_has_more(tc, &a_ci)) {
                    collation_adjust(tc, ai_coll_val, bi_coll_val, collation_mode,
                        MVM_string_grapheme_ci_get_codepoint(tc, &a_ci));
                }
            }
            else {
                collation_adjust(tc, ai_coll_val, bi_coll_val, collation_mode, ai);
            }

            if (bi < 0) {
                MVMCodepointIter b_ci;
                MVM_string_grapheme_ci_init(tc, &b_ci, bi);
                while (MVM_string_grapheme_ci_has_more(tc, &b_ci)) {
                    collation_adjust(tc, bi_coll_val, ai_coll_val, collation_mode,
                        MVM_string_grapheme_ci_get_codepoint(tc, &b_ci));
                }
            }
            else {
                collation_adjust(tc, bi_coll_val, ai_coll_val, collation_mode, bi);
            }
            /* Note if we are here we *already* know the codepoints are not equal */
            for (i = 0; i < 3; i++) {
                /* If collation values are not equal */
                if (ai_coll_val[i] != bi_coll_val[i])
                    rtrn = ai_coll_val[i] < bi_coll_val[i] ? -1 :
                           ai_coll_val[i] > bi_coll_val[i] ?  1 :
                                                              0 ;
                    if (rtrn != 0)
                        return rtrn;
            }
            /* If we don't have quaternary collation level set (we throw away codepoint info)
             * we know from the previous check that the collation values are equal */
            if ( !( collation_mode & (128 + 64) ) )
                continue;

            /* If we get here, then collation values were equal and we have
             * quaternary level enabled, so return by codepoint */
            if (collation_mode & 64) {
                return  ai < bi ? -1 :
                        ai > bi ?  1 :
                                   0 ;
            }
            else {
                return  ai < bi ?  1 :
                        ai > bi ? -1 :
                                   0 ;
            }
        }
    }

    /* If we don't have quaternary collation level set (we throw away codepoint info)
     * we should return 0 because we have gone through all codepoints we have */
    if ( !( collation_mode & (128 + 64) ) )
        return 0;

    /* If we get here, then collation values were equal and we have
     * quaternary level enabled, so return by length */

    /* If quaternary level is both enabled AND reversed, this negates itself
     * and it is thus ignored */
    if (collation_mode & 64 && collation_mode & 128) {
        return 0;
    }
    else if (collation_mode & 64) {
        return alen < blen ? -1 :
               alen > blen ?  1 :
                              0 ;
    }
    else if (collation_mode & 128) {
        return alen < blen ?  1 :
               alen > blen ? -1 :
                              0 ;
    }
    MVM_exception_throw_adhoc(tc, "unicmp_s end of function should not be reachable\n");
}

/* Looks up a codepoint by name. Lazily constructs a hash. */
MVMGrapheme32 MVM_unicode_lookup_by_name(MVMThreadContext *tc, MVMString *name) {
    MVMuint64 size;
    char *cname = MVM_string_utf8_encode_C_string(tc, name);
    size_t cname_len = strlen((const char *) cname );
    MVMUnicodeNameRegistry *result;
    if (!codepoints_by_name) {
        generate_codepoints_by_name(tc);
    }
    HASH_FIND(hash_handle, codepoints_by_name, cname, cname_len, result);

    MVM_free(cname);
    return result ? result->codepoint : -1;
}

MVMString * MVM_unicode_get_name(MVMThreadContext *tc, MVMint64 codepoint) {
    const char *name = NULL;

    /* Catch out-of-bounds code points. */
    if (codepoint < 0) {
        name = "<illegal>";
    }
    else if (0x10ffff < codepoint) {
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

MVMString * MVM_unicode_codepoint_get_property_str(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    const char * const str = MVM_unicode_get_property_str(tc, codepoint, property_code);

    if (!str)
        return tc->instance->str_consts.empty;

    return MVM_string_ascii_decode(tc, tc->instance->VMString, str, strlen(str));
}

const char * MVM_unicode_codepoint_get_property_cstr(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    return MVM_unicode_get_property_str(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_int(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code);
}

MVMint64 MVM_unicode_codepoint_get_property_bool(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code) {
    if (property_code == 0)
        return 0;
    return (MVMint64)MVM_unicode_get_property_int(tc, codepoint, property_code) != 0;
}

MVMint64 MVM_unicode_codepoint_has_property_value(MVMThreadContext *tc, MVMint64 codepoint, MVMint64 property_code, MVMint64 property_value_code) {
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
                while (0 < i && CaseFolding_grows_table[folding_index][i - 1] == 0)
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
                while (0 < i && SpecialCasing_table[special_casing_index][case_][i - 1] == 0)
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
static MVMUnicodeGraphemeNameRegistry *property_codes_by_seq_names;

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
static void generate_property_codes_by_seq_names(MVMThreadContext *tc) {
    MVMuint32 num_names = num_unicode_seq_keypairs;

    while (num_names--) {
        MVMUnicodeGraphemeNameRegistry *entry = MVM_malloc(sizeof(MVMUnicodeGraphemeNameRegistry));
        entry->name = (char *)uni_seq_pairs[num_names].name;
        entry->structindex = uni_seq_pairs[num_names].value;
        HASH_ADD_KEYPTR(hash_handle, property_codes_by_seq_names,
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
    MVMUnicodeNameRegistry **hash_array = MVM_calloc(MVM_NUM_PROPERTY_CODES, sizeof(MVMUnicodeNameRegistry *));
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
/* Quickly determines the length of a number 6.5x faster than doing log10 after
 * compiler optimization */
MVM_STATIC_INLINE size_t length_of_num (size_t number) {
    if (number < 10) return 1;
    return 1 + length_of_num(number / 10);
}
MVMint32 unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, MVMuint64 cname_length) {
    char *out_str = NULL;
    MVMUnicodeNameRegistry *result = NULL;
                                   /* number + dash + property_value + NULL */
    MVMuint64 out_str_length = length_of_num(property_code) + 1 + cname_length + 1;
    if (1024 < out_str_length)
        MVM_exception_throw_adhoc(tc, "Property value or name queried is larger than allowed.");

    out_str = alloca(sizeof(char) * out_str_length);
    snprintf(out_str, out_str_length, "%"PRIi64"-%s", property_code, cname);

    HASH_FIND(hash_handle, unicode_property_values_hashes[property_code], out_str, out_str_length - 1, result);
    return result ? result->codepoint : 0;
}
MVMint32 MVM_unicode_name_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, MVMString *name) {
    if (property_code <= 0 || MVM_NUM_PROPERTY_CODES <= property_code)
        return 0;
    else {
        MVMuint64 cname_length;
        char *cname = MVM_string_ascii_encode(tc, name, &cname_length, 0);
        return unicode_cname_to_property_value_code(tc, property_code, cname, cname_length);
    }
}
MVMint32 MVM_unicode_cname_to_property_value_code(MVMThreadContext *tc, MVMint64 property_code, const char *cname, size_t cname_length) {
    if (property_code <= 0 || MVM_NUM_PROPERTY_CODES <= property_code)
        return 0;
    else
        return unicode_cname_to_property_value_code(tc, property_code, cname, cname_length);
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
            MVMUnicodeNameRegistry *entry = NULL;
            MVMUnicodeNameRegistry *tmp   = NULL;
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
/* Looks up a codepoint sequence or codepoint by name (case insensitive).
 First tries to look it up by codepoint with MVM_unicode_lookup_by_name and if
 not found as a named codepoint, lazily constructs a hash of the codepoint
 sequences and looks up the sequence name */
MVMString * MVM_unicode_string_from_name(MVMThreadContext *tc, MVMString *name) {
    MVMString * name_uc = MVM_string_uc(tc, name);
    char * cname = NULL;
    MVMUnicodeGraphemeNameRegistry *result;

    MVMGrapheme32 result_graph = MVM_unicode_lookup_by_name(tc, name_uc);
    /* If it's just a codepoint, return that */
    if (0 <= result_graph) {
        return MVM_string_chr(tc, result_graph);
    }
    /* Otherwise look up the sequence */
    else {
        const MVMint32 *uni_seq = NULL;
        cname = MVM_string_utf8_encode_C_string(tc, name_uc);
        if (!property_codes_by_seq_names) {
            generate_property_codes_by_seq_names(tc);
        }
        HASH_FIND(hash_handle, property_codes_by_seq_names, cname, strlen((const char *)cname), result);
        MVM_free(cname);
        /* If we can't find a result return an empty string */
        if (!result)
            return tc->instance->str_consts.empty;

        uni_seq = uni_seq_enum[result->structindex];
        /* The first element is the number of codepoints in the sequence */
        return MVM_unicode_codepoints_c_array_to_nfg_string(tc, (MVMCodepoint *) uni_seq + 1, uni_seq[0]);
    }

}
