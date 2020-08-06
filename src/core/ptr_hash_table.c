#include "moar.h"

#define PTR_LOAD_FACTOR 0.75
#define PTR_INITIAL_SIZE 8
#define PTR_INITIAL_KEY_RIGHT_SHIFT (8 * sizeof(uintptr_t) - 3)

MVM_STATIC_INLINE MVMuint32 hash_true_size(MVMPtrHashTable *hashtable) {
    MVMuint32 true_size = hashtable->official_size + hashtable->max_items - 1;
    if (hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE < true_size) {
        true_size = hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE;
    }
    return true_size;
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_ptr_hash_demolish(MVMThreadContext *tc, MVMPtrHashTable *hashtable) {
    if (hashtable->metadata) {
        MVM_free(hashtable->entries);
        MVM_free(hashtable->metadata - 1);
    }
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE void hash_allocate_common(MVMPtrHashTable *hashtable) {
    hashtable->max_items = hashtable->official_size * PTR_LOAD_FACTOR;
    size_t actual_items = hash_true_size(hashtable);
    hashtable->entries = MVM_malloc(sizeof(struct MVMPtrHashEntry) * actual_items);
    hashtable->metadata = MVM_calloc(1 + actual_items + 1, 1);
    /* A sentinel. This marks an occupied slot, at its ideal position. */
    *hashtable->metadata = 1;
    ++hashtable->metadata;
    /* A sentinel at the other end. Again, occupited, ideal position. */
    hashtable->metadata[actual_items] = 1;
}

MVM_STATIC_INLINE void hash_initial_allocate(MVMPtrHashTable *hashtable) {
    hashtable->key_right_shift = PTR_INITIAL_KEY_RIGHT_SHIFT;
    hashtable->official_size = PTR_INITIAL_SIZE;

    hash_allocate_common(hashtable);
}

/* make sure you still have your copies of entries and metadata before you
   call this. */
MVM_STATIC_INLINE void hash_grow(MVMPtrHashTable *hashtable) {
    --hashtable->key_right_shift;
    hashtable->official_size *= 2;

    hash_allocate_common(hashtable);
}

MVM_STATIC_INLINE struct MVMPtrHashEntry *hash_insert_internal(MVMThreadContext *tc,
                                                               MVMPtrHashTable *hashtable,
                                                               const void *key) {
    if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %p",
                 key);
    }

    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_ptr_hash_code(key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries + bucket * sizeof(struct MVMPtrHashEntry);
    MVMuint8 *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata < probe_distance) {
            /* this is our slot. occupied or not, it is our rightful place. */

            if (*metadata == 0) {
                /* Open goal. Score! */
            } else {
                /* make room. */

                /* Optimisation first seen in Martin Ankerl's implementation -
                   we don't need actually implement the "stealing" by swapping
                   elements and carrying on with insert. The invariant of the
                   hash is that probe distances are never out of order, and as
                   all the following elements have probe distances in order, we
                   can maintain the invariant just as well by moving everything
                   along by one. */
                MVMuint8 *find_me_a_gap = metadata;
                MVMuint8 old_probe_distance = *metadata;
                do {
                    MVMuint8 new_probe_distance = 1 + old_probe_distance;
                    if (new_probe_distance == MVM_HASH_MAX_PROBE_DISTANCE) {
                        /* Optimisation from Martin Ankerl's implementation:
                           setting this to zero forces a resize on any insert,
                           *before* the actual insert, so that we never end up
                           having to handle overflow *during* this loop. This
                           loop can always complete. */
                        hashtable->max_items = 0;
                    }
                    /* a swap: */
                    old_probe_distance = *++find_me_a_gap;
                    *find_me_a_gap = new_probe_distance;
                } while (old_probe_distance);

                MVMuint32 entries_to_move = find_me_a_gap - metadata;
                memmove(entry_raw + sizeof(struct MVMPtrHashEntry), entry_raw,
                        sizeof(struct MVMPtrHashEntry) * entries_to_move);
            }

            *metadata = probe_distance;
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) entry_raw;
            entry->key = NULL;
            return entry;
        }

        if (*metadata == probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) entry_raw;
            if (entry->key == key) {
                return entry;
            }
        }
        ++probe_distance;
        ++metadata;
        entry_raw += sizeof(struct MVMPtrHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

struct MVMPtrHashEntry *MVM_ptr_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMPtrHashTable *hashtable,
                                                  const void *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        hash_initial_allocate(hashtable);
    }
    else if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        /* We should avoid growing the hash if we don't need to.
         * It's expensive, and for hashes with iterators, growing the hash
         * invalidates iterators. Which is buggy behaviour if the fetch doesn't
         * need to create a key. */
        struct MVMPtrHashEntry *entry = MVM_ptr_hash_fetch(tc, hashtable, key);
        if (entry) {
            return entry;
        }

        MVMuint32 true_size =  hash_true_size(hashtable);
        char *entry_raw_orig = hashtable->entries;
        MVMuint8 *metadata_orig = hashtable->metadata;

        hash_grow(hashtable);

        char *entry_raw = entry_raw_orig;
        MVMuint8 *metadata = metadata_orig;
        MVMHashNumItems bucket = 0;
        while (bucket < true_size) {
            if (*metadata) {
                struct MVMPtrHashEntry *old_entry = (struct MVMPtrHashEntry *) entry_raw;
                struct MVMPtrHashEntry *new_entry =
                    hash_insert_internal(tc, hashtable, old_entry->key);
                assert(new_entry->key == NULL);
                *new_entry = *old_entry;
            }
            ++bucket;
            ++metadata;
            entry_raw += sizeof(struct MVMPtrHashEntry);
        }
        MVM_free(entry_raw_orig);
        MVM_free(metadata_orig - 1);
    }
    struct MVMPtrHashEntry *new_entry
        = hash_insert_internal(tc, hashtable, key);
    if (!new_entry->key) {
        ++hashtable->cur_items;
    }
    return new_entry;
}

uintptr_t MVM_ptr_hash_fetch_and_delete(MVMThreadContext *tc,
                                        MVMPtrHashTable *hashtable,
                                        const void *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        /* Should this be an oops? */
        return 0;
    }
    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_ptr_hash_code(key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries + bucket * sizeof(struct MVMPtrHashEntry);
    uint8_t *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata == probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) entry_raw;
            if (entry->key == key) {
                /* Target acquired. */
                uintptr_t retval = entry->value;

                uint8_t *metadata_target = metadata;
                /* Look at the next slot */
                uint8_t old_probe_distance = metadata_target[1];
                while (old_probe_distance > 1) {
                    /* OK, we can move this one. */
                    *metadata_target = old_probe_distance - 1;
                    /* Try the next one, etc */
                    ++metadata_target;
                    old_probe_distance = metadata_target[1];
                }
                /* metadata_target now points to the metadata for the last thing
                   we did move. (possibly still our target). */

                uint32_t entries_to_move = metadata_target - metadata;
                if (entries_to_move) {
                    memmove(entry_raw, entry_raw + sizeof(struct MVMPtrHashEntry),
                            sizeof(struct MVMPtrHashEntry) * entries_to_move);
                }
                /* and this slot is now emtpy. */
                *metadata_target = 0;
                --hashtable->cur_items;

                /* Job's a good 'un. */
                return retval;
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
            /* Strange. Not in the hash. Should this be an oops? */
            return 0;
        }
        ++probe_distance;
        ++metadata;
        entry_raw += sizeof(struct MVMPtrHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}
