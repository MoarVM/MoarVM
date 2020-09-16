/* Frees the entire contents of the hash, leaving you just the hashtable itself,
 * which you allocated (heap, stack, inside another struct, wherever) */
void MVM_uni_hash_demolish(MVMThreadContext *tc, MVMUniHashTable *hashtable);
/* and then free memory if you allocated it */

void MVM_uni_hash_initial_allocate(MVMThreadContext *tc,
                                   MVMUniHashTable *hashtable,
                                   MVMuint32 entries);

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory for the hashtable struct itself - you can embed the
 * struct within a larger struct if you wish.
 */
MVM_STATIC_INLINE void MVM_uni_hash_build(MVMThreadContext *tc,
                                          MVMUniHashTable *hashtable,
                                          MVMuint32 entries) {
    memset(hashtable, 0, sizeof(*hashtable));
    if (entries) {
        MVM_uni_hash_initial_allocate(tc, hashtable, entries);
    }
}

/* Unicode names (etc) are not under the control of an external attacker [:-)]
 * so we don't need a cryptographic hash function here. I'm assuming that
 * 32 bit FNV1a is good enough and fast enough to be useful. */
MVM_STATIC_INLINE MVMuint32 MVM_uni_hash_code(const char *key, size_t len) {
    const char *const end = key + len;
    MVMuint32 hash = 0x811c9dc5;
    while (key < end) {
        hash ^= *key++;
        hash *= 0x01000193;
    }
    /* "finalise" it by multiplying by the constant for Fibonacci bucket
       determination. */
    return hash * 0x9e3779b7;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_uni_hash_insert(MVMThreadContext *tc,
                         MVMUniHashTable *hashtable,
                         const char *key,
                         MVMint32 value);

MVM_STATIC_INLINE struct MVMUniHashEntry *MVM_uni_hash_fetch(MVMThreadContext *tc,
                                                              MVMUniHashTable *hashtable,
                                                              const char *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        return NULL;
    }
    unsigned int probe_distance = 1;
    MVMuint32 hash_val = MVM_uni_hash_code(key, strlen(key));
    MVMHashNumItems bucket = hash_val >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries - bucket * sizeof(struct MVMUniHashEntry);
    MVMuint8 *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata == probe_distance) {
            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
            if (entry->hash_val == hash_val && 0 == strcmp(entry->key, key)) {
                return entry;
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
        entry_raw -= sizeof(struct MVMUniHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

MVM_STATIC_INLINE MVMHashNumItems MVM_uni_hash_count(MVMThreadContext *tc,
                                                     MVMUniHashTable *hashtable) {
    return hashtable->cur_items;
}
