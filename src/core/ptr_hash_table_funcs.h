/* These are private. We need them out here for the inline functions. Use those.
 */
/* See comments in hash_allocate_common (and elsewhere) before changing the
 * load factor, or PTR_MIN_SIZE_BASE_2 or MVM_HASH_INITIAL_BITS_IN_METADATA,
 * and test with assertions enabled. The current choices permit certain
 * optimisation assumptions in parts of the code. */
#define MVM_PTR_HASH_LOAD_FACTOR 0.75
MVM_STATIC_INLINE MVMuint32 MVM_ptr_hash_official_size(const struct MVMPtrHashTableControl *control) {
    return 1 << (MVMuint32)control->official_size_log2;
}
/* -1 because...
 * probe distance of 1 is the correct bucket.
 * hence for a value whose ideal slot is the last bucket, it's *in* the official
 * allocation.
 * probe distance of 2 is the first extra bucket beyond the official allocation
 * probe distance of 255 is the 254th beyond the official allocation.
 */
MVM_STATIC_INLINE MVMuint32 MVM_ptr_hash_allocated_items(const struct MVMPtrHashTableControl *control) {
    return MVM_ptr_hash_official_size(control) + control->max_probe_distance_limit - 1;
}
MVM_STATIC_INLINE MVMuint32 MVM_ptr_hash_max_items(const struct MVMPtrHashTableControl *control) {
    return MVM_ptr_hash_official_size(control) * MVM_PTR_HASH_LOAD_FACTOR;
}
MVM_STATIC_INLINE MVMuint8 *MVM_ptr_hash_metadata(const struct MVMPtrHashTableControl *control) {
    return (MVMuint8 *) control + sizeof(struct MVMPtrHashTableControl);
}
MVM_STATIC_INLINE MVMuint8 *MVM_ptr_hash_entries(const struct MVMPtrHashTableControl *control) {
    return (MVMuint8 *) control - sizeof(struct MVMPtrHashEntry);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_ptr_hash_demolish(MVMThreadContext *tc, MVMPtrHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_ptr_hash_build(MVMThreadContext *tc, MVMPtrHashTable *hashtable) {
    hashtable->table = NULL;
}

MVM_STATIC_INLINE int MVM_ptr_hash_is_empty(MVMThreadContext *tc,
                                            MVMPtrHashTable *hashtable) {
    struct MVMPtrHashTableControl *control = hashtable->table;
    return !control || control->cur_items == 0;
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

MVM_STATIC_INLINE struct MVM_hash_loop_state
MVM_ptr_hash_create_loop_state(struct MVMPtrHashTableControl *control,
                               const void *key) {
    struct MVM_hash_loop_state retval;
    retval.entry_size = sizeof(struct MVMPtrHashEntry);
    retval.metadata_increment = 1 << control->metadata_hash_bits;
    retval.metadata_hash_mask = retval.metadata_increment - 1;
    retval.probe_distance_shift = control->metadata_hash_bits;
    retval.max_probe_distance = control->max_probe_distance;

    unsigned int used_hash_bits
        = MVM_ptr_hash_code(key) >> (control->key_right_shift - control->metadata_hash_bits);
    retval.probe_distance = retval.metadata_increment | (used_hash_bits & retval.metadata_hash_mask);
    MVMHashNumItems bucket = used_hash_bits >> control->metadata_hash_bits;
    if (!control->metadata_hash_bits) {
        assert(retval.probe_distance == 1);
        assert(retval.metadata_hash_mask == 0);
        assert(bucket == used_hash_bits);
    }

    retval.entry_raw = MVM_ptr_hash_entries(control) - bucket * retval.entry_size;
    retval.metadata = MVM_ptr_hash_metadata(control) + bucket;
    return retval;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_ptr_hash_insert(MVMThreadContext *tc,
                         MVMPtrHashTable *hashtable,
                         const void *key,
                         uintptr_t value);

MVM_STATIC_INLINE struct MVMPtrHashEntry *MVM_ptr_hash_fetch(MVMThreadContext *tc,
                                                             MVMPtrHashTable *hashtable,
                                                             const void *key) {
    if (MVM_ptr_hash_is_empty(tc, hashtable)) {
        return NULL;
    }

    struct MVMPtrHashTableControl *control = hashtable->table;
    struct MVM_hash_loop_state ls = MVM_ptr_hash_create_loop_state(control, key);

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            if (entry->key == key) {
                return entry;
            }
        }
        /* There's a sentinel at the end. This will terminate: */
        if (*ls.metadata < ls.probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced further on. Hence, the key
               we seek can't be in the hash table. */
            return NULL;
        }
        ls.probe_distance += ls.metadata_increment;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance < (ls.max_probe_distance + 2) * ls.metadata_increment);
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + MVM_ptr_hash_max_items(control));
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + 256);
    }
}

struct MVMPtrHashEntry *MVM_ptr_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMPtrHashTable *hashtable,
                                                  const void *key);

uintptr_t MVM_ptr_hash_fetch_and_delete(MVMThreadContext *tc,
                                        MVMPtrHashTable *hashtable,
                                        const void *key);
