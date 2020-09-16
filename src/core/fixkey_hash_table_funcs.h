/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_fixkey_hash_demolish(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_fixkey_hash_build(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable, MVMuint32 entry_size) {
    if (MVM_UNLIKELY(entry_size == 0 || entry_size > 1024 || entry_size & 3)) {
        MVM_oops(tc, "Hash table entry_size %" PRIu32 " is invalid", entry_size);
    }
    memset(hashtable, 0, sizeof(*hashtable));
    hashtable->entry_size = entry_size;
}

MVM_STATIC_INLINE MVMuint64 MVM_fixkey_hash_code(MVMThreadContext *tc, MVMString *key) {
    return MVM_string_hash_code(tc, key) * UINT64_C(11400714819323198485);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void *MVM_fixkey_hash_insert_nocheck(MVMThreadContext *tc,
                                     MVMFixKeyHashTable *hashtable,
                                     MVMString *key);

MVM_STATIC_INLINE void *MVM_fixkey_hash_fetch_nocheck(MVMThreadContext *tc,
                                                      MVMFixKeyHashTable *hashtable,
                                                      MVMString *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        return NULL;
    }
    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_fixkey_hash_code(tc, key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries - bucket * sizeof(MVMString ***);
    MVMuint8 *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata == probe_distance) {
            MVMString ***entry = (MVMString ***) entry_raw;
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
        if (*metadata < probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced futher on. Hence, the key
               we seek can't be in the hash table. */
            return NULL;
        }
        ++probe_distance;
        ++metadata;
        entry_raw -= sizeof(MVMString ***);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
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
