#include "moar.h"

#define PTR_INITIAL_SIZE_LOG2 3
#define PTR_INITIAL_KEY_RIGHT_SHIFT (8 * sizeof(uintptr_t) - 3)

MVM_STATIC_INLINE MVMuint32 hash_true_size(const struct MVMPtrHashTableControl *control) {
    return MVM_ptr_hash_official_size(control) + control->probe_overflow_size;
}

MVM_STATIC_INLINE void hash_demolish_internal(MVMThreadContext *tc,
                                              struct MVMPtrHashTableControl *control) {
    size_t actual_items = hash_true_size(control);
    size_t entries_size = sizeof(struct MVMPtrHashEntry) * actual_items;
    char *start = (char *)control - entries_size;
    MVM_free(start);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_ptr_hash_demolish(MVMThreadContext *tc, MVMPtrHashTable *hashtable) {
    struct MVMPtrHashTableControl *control = hashtable->table;
    if (!control)
        return;
    hash_demolish_internal(tc, control);
    hashtable->table = NULL;
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE struct MVMPtrHashTableControl *hash_allocate_common(MVMThreadContext *tc,
                                                                      MVMuint8 key_right_shift,
                                                                      MVMuint8 official_size_log2) {
    MVMuint32 official_size = 1 << (MVMuint32)official_size_log2;
    MVMuint32 max_items = official_size * MVM_PTR_HASH_LOAD_FACTOR;
    MVMuint32 overflow_size = max_items - 1;
    /* -1 because...
     * probe distance of 1 is the correct bucket.
     * hence for a value whose ideal slot is the last bucket, it's *in* the
     * official allocation.
     * probe distance of 2 is the first extra bucket beyond the official
     * allocation
     * probe distance of 255 is the 254th beyond the official allocation.
     */
    MVMuint8 probe_overflow_size;
    if (MVM_HASH_MAX_PROBE_DISTANCE < overflow_size) {
        probe_overflow_size = MVM_HASH_MAX_PROBE_DISTANCE - 1;
    } else {
        probe_overflow_size = overflow_size;
    }
    size_t actual_items = official_size + probe_overflow_size;
    size_t entries_size = sizeof(struct MVMPtrHashEntry) * actual_items;
    size_t metadata_size = MVM_hash_round_size_up(actual_items + 1);
    size_t total_size
        = entries_size + sizeof(struct MVMPtrHashTableControl) + metadata_size;

    struct MVMPtrHashTableControl *control =
        (struct MVMPtrHashTableControl *) ((char *)MVM_malloc(total_size) + entries_size);

    control->official_size_log2 = official_size_log2;
    control->max_items = max_items;
    control->cur_items = 0;
    control->probe_overflow_size = probe_overflow_size;
    control->key_right_shift = key_right_shift;

    MVMuint8 *metadata = (MVMuint8 *)(control + 1);
    memset(metadata, 0, metadata_size);

    /* A sentinel. This marks an occupied slot, at its ideal position. */
    metadata[actual_items] = 1;

    return control;
}

MVM_STATIC_INLINE struct MVMPtrHashEntry *hash_insert_internal(MVMThreadContext *tc,
                                                               struct MVMPtrHashTableControl *control,
                                                               const void *key) {
    if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %p",
                 key);
    }

    struct MVM_hash_loop_state ls = MVM_ptr_hash_create_loop_state(control, key);

    while (1) {
        if (*ls.metadata < ls.probe_distance) {
            /* this is our slot. occupied or not, it is our rightful place. */

            if (*ls.metadata == 0) {
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
                MVMuint8 *find_me_a_gap = ls.metadata;
                MVMuint8 old_probe_distance = *ls.metadata;
                do {
                    MVMuint8 new_probe_distance = 1 + old_probe_distance;
                    if (new_probe_distance == MVM_HASH_MAX_PROBE_DISTANCE) {
                        /* Optimisation from Martin Ankerl's implementation:
                           setting this to zero forces a resize on any insert,
                           *before* the actual insert, so that we never end up
                           having to handle overflow *during* this loop. This
                           loop can always complete. */
                        control->max_items = 0;
                    }
                    /* a swap: */
                    old_probe_distance = *++find_me_a_gap;
                    *find_me_a_gap = new_probe_distance;
                } while (old_probe_distance);

                MVMuint32 entries_to_move = find_me_a_gap - ls.metadata;
                size_t size_to_move = ls.entry_size * entries_to_move;
                /* When we had entries *ascending* this was
                 * memmove(entry_raw + sizeof(struct MVMPtrHashEntry), entry_raw,
                 *         sizeof(struct MVMPtrHashEntry) * entries_to_move);
                 * because we point to the *start* of the block of memory we
                 * want to move, and we want to move it one "entry" forwards.
                 * `entry_raw` is still a pointer to where we want to make free
                 * space, but what want to do now is move everything at it and
                 * *before* it downwards. */
                MVMuint8 *dest = ls.entry_raw - size_to_move;
                memmove(dest, dest + ls.entry_size, size_to_move);
            }

            /* The same test and optimisation as in the "make room" loop - we're
             * about to insert something at the (current) max_probe_distance, so
             * signal to the next insertion that it needs to take action first.
             */
            if (ls.probe_distance == MVM_HASH_MAX_PROBE_DISTANCE) {
                control->max_items = 0;
            }

            ++control->cur_items;

            *ls.metadata = ls.probe_distance;
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            entry->key = NULL;
            return entry;
        }

        if (*ls.metadata == ls.probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            if (entry->key == key) {
                return entry;
            }
        }
        ++ls.probe_distance;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + MVM_ptr_hash_max_items(control));
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + 256);
    }
}

struct MVMPtrHashEntry *MVM_ptr_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMPtrHashTable *hashtable,
                                                  const void *key) {
    struct MVMPtrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(!control)) {
        control = hash_allocate_common(tc,
                                       PTR_INITIAL_KEY_RIGHT_SHIFT,
                                       PTR_INITIAL_SIZE_LOG2);
        hashtable->table = control;
    }
    else if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        /* We should avoid growing the hash if we don't need to.
         * It's expensive, and for hashes with iterators, growing the hash
         * invalidates iterators. Which is buggy behaviour if the fetch doesn't
         * need to create a key. */
        struct MVMPtrHashEntry *entry = MVM_ptr_hash_fetch(tc, hashtable, key);
        if (entry) {
            return entry;
        }

        MVMuint32 true_size =  hash_true_size(control);
        MVMuint8 *entry_raw_orig = MVM_ptr_hash_entries(control);
        MVMuint8 *metadata_orig = MVM_ptr_hash_metadata(control);

        struct MVMPtrHashTableControl *control_orig = control;

        control = hash_allocate_common(tc,
                                       control_orig->key_right_shift - 1,
                                       control_orig->official_size_log2 + 1);

        hashtable->table = control;

        MVMuint8 *entry_raw = entry_raw_orig;
        MVMuint8 *metadata = metadata_orig;
        MVMHashNumItems bucket = 0;
        while (bucket < true_size) {
            if (*metadata) {
                struct MVMPtrHashEntry *old_entry = (struct MVMPtrHashEntry *) entry_raw;
                struct MVMPtrHashEntry *new_entry =
                    hash_insert_internal(tc, control, old_entry->key);
                assert(new_entry->key == NULL);
                *new_entry = *old_entry;
            }
            ++bucket;
            ++metadata;
            entry_raw -= sizeof(struct MVMPtrHashEntry);
        }
        hash_demolish_internal(tc, control_orig);
    }
    return hash_insert_internal(tc, control, key);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care.
 * (well that's the official line. As you can see, the XXX suggests we currently
 * don't exploit the documented freedom.) */
void MVM_ptr_hash_insert(MVMThreadContext *tc,
                         MVMPtrHashTable *hashtable,
                         const void *key,
                         uintptr_t value) {
    struct MVMPtrHashEntry *new_entry = MVM_ptr_hash_lvalue_fetch(tc, hashtable, key);
    if (new_entry->key) {
        if (value != new_entry->value) {
            MVMHashNumItems bucket = MVM_ptr_hash_code(key) >> hashtable->table->key_right_shift;
            /* definately XXX - what should we do here? */
            MVM_oops(tc, "insert conflict, %p is %u, %"PRIu64" != %"PRIu64,
                     key, bucket, (MVMuint64) value, (MVMuint64) new_entry->value);
        }
    } else {
        new_entry->key = key;
        new_entry->value = value;
    }
}

uintptr_t MVM_ptr_hash_fetch_and_delete(MVMThreadContext *tc,
                                        MVMPtrHashTable *hashtable,
                                        const void *key) {
    if (MVM_ptr_hash_is_empty(tc, hashtable)) {
        return 0;
    }

    struct MVMPtrHashTableControl *control = hashtable->table;
    struct MVM_hash_loop_state ls = MVM_ptr_hash_create_loop_state(control, key);

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            if (entry->key == key) {
                /* Target acquired. */
                uintptr_t retval = entry->value;

                uint8_t *metadata_target = ls.metadata;
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

                uint32_t entries_to_move = metadata_target - ls.metadata;
                if (entries_to_move) {
                    size_t size_to_move = ls.entry_size * entries_to_move;
                    /* When we had entries *ascending* in memory, this was
                     * memmove(entry_raw, entry_raw + sizeof(struct MVMPtrHashEntry),
                     *         sizeof(struct MVMPtrHashEntry) * entries_to_move);
                     * because we point to the *start* of the block of memory we
                     * want to move, and we want to move the block one "entry"
                     * backwards.
                     * `entry_raw` is still a pointer to the entry that we need
                     * to ovewrite, but now we need to move everything *before*
                     * it upwards to close the gap. */
                    memmove(ls.entry_raw - size_to_move + ls.entry_size,
                            ls.entry_raw - size_to_move,
                            size_to_move);
                }
                /* and this slot is now emtpy. */
                *metadata_target = 0;
                --control->cur_items;

                /* Job's a good 'un. */
                return retval;
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
            /* Strange. Not in the hash. Should this be an oops? */
            return 0;
        }
        ++ls.probe_distance;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + MVM_ptr_hash_max_items(control));
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + 256);
    }
}
