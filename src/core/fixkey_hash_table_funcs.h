/* These are private. We need them out here for the inline functions. Use those.
 */
#define MVM_FIXKEY_HASH_LOAD_FACTOR 0.75
MVM_STATIC_INLINE MVMuint32 MVM_fixkey_hash_official_size(const struct MVMFixKeyHashTableControl *control) {
    return 1 << (MVMuint32)control->official_size_log2;
}
MVM_STATIC_INLINE MVMuint32 MVM_fixkey_hash_allocated_items(const struct MVMFixKeyHashTableControl *control) {
    return MVM_fixkey_hash_official_size(control) + control->max_probe_distance_limit;
}
MVM_STATIC_INLINE MVMuint32 MVM_fixkey_hash_max_items(const struct MVMFixKeyHashTableControl *control) {
    return MVM_fixkey_hash_official_size(control) * MVM_FIXKEY_HASH_LOAD_FACTOR;
}
MVM_STATIC_INLINE MVMuint8 *MVM_fixkey_hash_metadata(const struct MVMFixKeyHashTableControl *control) {
    return (MVMuint8 *) control + sizeof(struct MVMFixKeyHashTableControl);
}
MVM_STATIC_INLINE MVMuint8 *MVM_fixkey_hash_entries(const struct MVMFixKeyHashTableControl *control) {
    return (MVMuint8 *) control - sizeof(MVMString ***);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_fixkey_hash_demolish(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
void MVM_fixkey_hash_build(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable, MVMuint32 entry_size);

MVM_STATIC_INLINE int MVM_fixkey_hash_is_empty(MVMThreadContext *tc,
                                               MVMFixKeyHashTable *hashtable) {
    struct MVMFixKeyHashTableControl *control = hashtable->table;
    return !control || control->cur_items == 0;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void *MVM_fixkey_hash_insert_nocheck(MVMThreadContext *tc,
                                     MVMFixKeyHashTable *hashtable,
                                     MVMString *key);


MVM_STATIC_INLINE struct MVM_hash_loop_state
MVM_fixkey_hash_create_loop_state(MVMThreadContext *tc,
                                  struct MVMFixKeyHashTableControl *control,
                                  MVMString *key) {
    MVMuint64 hash_val = MVM_string_hash_code(tc, key);
    MVMHashNumItems bucket = hash_val >> control->key_right_shift;
    struct MVM_hash_loop_state retval;
    retval.probe_distance = 1;
    retval.entry_size = sizeof(MVMString ***);
    retval.entry_raw = MVM_fixkey_hash_entries(control) - bucket * retval.entry_size;
    retval.metadata = MVM_fixkey_hash_metadata(control) + bucket;
    return retval;
}

MVM_STATIC_INLINE void *MVM_fixkey_hash_fetch_nocheck(MVMThreadContext *tc,
                                                      MVMFixKeyHashTable *hashtable,
                                                      MVMString *key) {
    if (MVM_fixkey_hash_is_empty(tc, hashtable)) {
        return NULL;
    }

    struct MVMFixKeyHashTableControl *control = hashtable->table;
    struct MVM_hash_loop_state ls = MVM_fixkey_hash_create_loop_state(tc, control, key);

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            MVMString ***entry = (MVMString ***) ls.entry_raw;
            /* A struct, which starts with an MVMString * */
            MVMString **indirection = *entry;
            if (*indirection == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, *indirection)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           *indirection, 0))) {
                return indirection;
            }
        }
        /* There's a sentinel at the end. This will terminate: */
        if (*ls.metadata < ls.probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced futher on. Hence, the key
               we seek can't be in the hash table. */
            return NULL;
        }
        ++ls.probe_distance;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= (unsigned int) control->max_probe_distance + 1);
        assert(ls.metadata < MVM_fixkey_hash_metadata(control) + MVM_fixkey_hash_official_size(control) + MVM_fixkey_hash_max_items(control));
        assert(ls.metadata < MVM_fixkey_hash_metadata(control) + MVM_fixkey_hash_official_size(control) + 256);
    }
}

/* Looks up entry for key, creating it if necessary.
 * Returns the structure we indirect to.
 * If it's freshly allocated, then *entry is NULL (you need to fill this in)
 * and everything else is uninitialised.
 * This might seem like a quirky API, but it's intended to fill a common pattern
 * we have, and the use of NULL key avoids needing two return values.
 * DON'T FORGET to fill in the NULL key. */
void *MVM_fixkey_hash_lvalue_fetch_nocheck(MVMThreadContext *tc,
                                           MVMFixKeyHashTable *hashtable,
                                           MVMString *key);
