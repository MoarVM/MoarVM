/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_ptr_hash_demolish(MVMThreadContext *tc, MVMPtrHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_ptr_hash_build(MVMThreadContext *tc, MVMPtrHashTable *hashtable) {
    memset(hashtable, 0, sizeof(*hashtable));
}

/* Fibonacci bucket determination.
 * pointers are not under the control of the external (ab)user, so we don't need
 * a crypographic hash. Moreover, "good enough" is likely better than "perfect"
 * at actual hashing, so until proven otherwise, we'll "just" multiply by the
 * golden ratio and then downshift to get a bucket. This will mix in all the
 * bits of the pointer, so avoids "obvious" problems such as pointers being
 * 8 or 16 byte aligned (lots of zeros in the least significant bits)
 * similarly the potential for lots of repetition in the higher bits.
 *
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 * If the size of the hashv is changed we will need to change max_hashv_div_phi,
 * to be max_hashv / phi rounded to the nearest *odd* number.
 * max_hashv / phi = 11400714819323198485 */

#if 8 <= MVM_PTR_SIZE
MVM_STATIC_INLINE MVMuint64 MVM_ptr_hash_code(const void *ptr) {
    return ((uintptr_t)ptr) * UINT64_C(11400714819323198485);
}
#else
MVM_STATIC_INLINE MVMuint32 MVM_ptr_hash_code(const void *ptr) {
    return ((uintptr_t)ptr) * 0x9e3779b7;
}
#endif

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_ptr_hash_insert(MVMThreadContext *tc,
                         MVMPtrHashTable *hashtable,
                         const void *key,
                         uintptr_t value);

MVM_STATIC_INLINE struct MVMPtrHashEntry *MVM_ptr_hash_fetch(MVMThreadContext *tc,
                                                             MVMPtrHashTable *hashtable,
                                                             const void *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        return NULL;
    }
    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_ptr_hash_code(key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries - bucket * sizeof(struct MVMPtrHashEntry);
    MVMuint8 *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata == probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) entry_raw;
            if (entry->key == key) {
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
        entry_raw -= sizeof(struct MVMPtrHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

struct MVMPtrHashEntry *MVM_ptr_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMPtrHashTable *hashtable,
                                                  const void *key);

uintptr_t MVM_ptr_hash_fetch_and_delete(MVMThreadContext *tc,
                                        MVMPtrHashTable *hashtable,
                                        const void *key);
