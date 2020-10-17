#include "moar.h"

#define PTR_INITIAL_SIZE_LOG2 3
#define PTR_INITIAL_KEY_RIGHT_SHIFT (8 * sizeof(uintptr_t) - 3)

MVM_STATIC_INLINE void hash_demolish_internal(MVMThreadContext *tc,
                                              struct MVMPtrHashTableControl *control) {
    size_t allocated_items = MVM_ptr_hash_allocated_items(control);
    size_t entries_size = sizeof(struct MVMPtrHashEntry) * allocated_items;
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
    MVMuint8 max_probe_distance_limit;
    if (MVM_HASH_MAX_PROBE_DISTANCE < max_items) {
        max_probe_distance_limit = MVM_HASH_MAX_PROBE_DISTANCE;
    } else {
        max_probe_distance_limit = max_items;
    }
    size_t allocated_items = official_size + max_probe_distance_limit - 1;
    size_t entries_size = sizeof(struct MVMPtrHashEntry) * allocated_items;
    size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
    size_t total_size
        = entries_size + sizeof(struct MVMPtrHashTableControl) + metadata_size;

    struct MVMPtrHashTableControl *control =
        (struct MVMPtrHashTableControl *) ((char *)MVM_malloc(total_size) + entries_size);

    control->official_size_log2 = official_size_log2;
    control->max_items = max_items;
    control->cur_items = 0;
    control->metadata_hash_bits = MVM_HASH_INITIAL_BITS_IN_METADATA;
    /* ie 7: */
    MVMuint8 initial_probe_distance = (1 << (8 - MVM_HASH_INITIAL_BITS_IN_METADATA)) - 1;
    control->max_probe_distance = max_probe_distance_limit > initial_probe_distance ? initial_probe_distance : max_probe_distance_limit;
    control->max_probe_distance_limit = max_probe_distance_limit;
    control->key_right_shift = key_right_shift;

    MVMuint8 *metadata = (MVMuint8 *)(control + 1);
    memset(metadata, 0, metadata_size);

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
                    MVMuint32 new_probe_distance = ls.metadata_increment + old_probe_distance;
                    if (new_probe_distance >> ls.probe_distance_shift == ls.max_probe_distance) {
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
            if (ls.probe_distance >> ls.probe_distance_shift == control->max_probe_distance) {
                control->max_items = 0;
            }

            ++control->cur_items;

            *ls.metadata = ls.probe_distance;
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            entry->key = NULL;
            return entry;
        }
        else if (*ls.metadata == ls.probe_distance) {
            struct MVMPtrHashEntry *entry = (struct MVMPtrHashEntry *) ls.entry_raw;
            if (entry->key == key) {
                return entry;
            }
        }
        ls.probe_distance += ls.metadata_increment;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;

        /* For insert, the loop must not iterate to any probe distance greater
         * than the (current) maximum probe distance, because it must never
         * insert an entry at a location beyond the maximum probe distance.
         *
         * For fetch and delete, the loop is permitted to reach (and read) one
         * beyond the maximum probe distance (hence +2 in the seemingly
         * analogous assertions) - but if so, it will always read from the
         * metadata a probe distance which is lower than the current probe
         * distance, and hence hit "not found" and terminate the loop.
         *
         * This is how the loop terminates when the max probe distance is
         * reached without needing an explicit test for it, and why we need an
         * initialised sentinel byte at the end of the metadata. */

        assert(ls.probe_distance < (ls.max_probe_distance + 1) * ls.metadata_increment);
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + MVM_ptr_hash_max_items(control));
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + 256);
    }
}

static struct MVMPtrHashTableControl *maybe_grow_hash(MVMThreadContext *tc,
                                                      struct MVMPtrHashTableControl *control) {
    /* control->max_items may have been set to 0 to trigger a call into this
     * function. */
    MVMuint32 max_items = MVM_ptr_hash_max_items(control);
    MVMuint32 max_probe_distance = control->max_probe_distance;
    MVMuint32 max_probe_distance_limit = control->max_probe_distance_limit;

    /* We can hit both the probe limit and the max items on the same insertion.
     * In which case, upping the probe limit isn't going to save us :-)
     * But if we hit the probe limit max (even without hitting the max items)
     * then we don't have more space in the metadata, so we're going to have to
     * grow anyway. */
    if (control->cur_items < max_items
        && max_probe_distance < max_probe_distance_limit) {
        /* We hit the probe limit, but not the max items count. */
        MVMuint32 new_probe_distance = 1 + 2 * max_probe_distance;
        if (new_probe_distance > max_probe_distance_limit) {
            new_probe_distance = max_probe_distance_limit;
        }

        MVMuint8 *metadata = MVM_ptr_hash_metadata(control);
        MVMuint32 in_use_items = MVM_ptr_hash_official_size(control) + max_probe_distance;
        /* not `in_use_items + 1` because because we don't need to shift the
         * sentinel. */
        size_t metadata_size = MVM_hash_round_size_up(in_use_items);
        size_t loop_count = metadata_size / sizeof(unsigned long);
        unsigned long *p = (unsigned long *) metadata;
        /* right shift each byte by 1 bit, clearing the top bit. */
        do {
            /* 0x7F7F7F7F7F7F7F7F on 64 bit systems, 0x7F7F7F7F on 32 bit,
             * but without using constants or shifts larger than 32 bits, or
             * the preprocessor. (So the compiler checks all code everywhere.)
             * Will break on a system with 128 bit longs. */
            *p = (*p >> 1) & (0x7F7F7F7FUL | (0x7F7F7F7FUL << (4 * sizeof(long))));
            ++p;
        } while (--loop_count);
        assert(control->metadata_hash_bits);
        --control->metadata_hash_bits;

        control->max_probe_distance = new_probe_distance;
        /* Reset this to its proper value. */
        control->max_items = max_items;
        assert(control->max_items);
        return NULL;
    }

    MVMuint32 entries_in_use = MVM_ptr_hash_official_size(control) + control->max_probe_distance - 1;
    MVMuint8 *entry_raw_orig = MVM_ptr_hash_entries(control);
    MVMuint8 *metadata_orig = MVM_ptr_hash_metadata(control);

    struct MVMPtrHashTableControl *control_orig = control;

    control = hash_allocate_common(tc,
                                   control_orig->key_right_shift - 1,
                                   control_orig->official_size_log2 + 1);

    MVMuint8 *entry_raw = entry_raw_orig;
    MVMuint8 *metadata = metadata_orig;
    MVMHashNumItems bucket = 0;
    while (bucket < entries_in_use) {
        if (*metadata) {
            struct MVMPtrHashEntry *old_entry = (struct MVMPtrHashEntry *) entry_raw;
            struct MVMPtrHashEntry *new_entry =
                hash_insert_internal(tc, control, old_entry->key);
            assert(new_entry->key == NULL);
            *new_entry = *old_entry;

            if (!control->max_items) {
                /* Probably we hit the probe limit.
                 * But it's just possible that one actual "grow" wasn't enough.
                 */
                struct MVMPtrHashTableControl *new_control
                    = maybe_grow_hash(tc, control);
                if (new_control) {
                    control = new_control;
                }
            }
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(struct MVMPtrHashEntry);
    }
    hash_demolish_internal(tc, control_orig);
    return control;
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

        struct MVMPtrHashTableControl *new_control = maybe_grow_hash(tc, control);
        if (new_control) {
            /* We could unconditionally assign this, but that would mean CPU
             * cache writes even when it was unchanged, and the address of
             * hashtable will not be in the same cache lines as we are writing
             * for the hash internals, so it will churn the write cache. */
            hashtable->table = control = new_control;
        }
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
                /* Without this, gcc seemed always to want to recalculate this
                 * for each loop iteration. Also, expressing this only in terms
                 * of ls.metadata_increment avoids 1 load (albeit from cache) */
                const uint8_t can_move = 2 * ls.metadata_increment;
                while (old_probe_distance >= can_move) {
                    /* OK, we can move this one. */
                    *metadata_target = old_probe_distance - ls.metadata_increment;
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
        else if (*ls.metadata < ls.probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced further on. Hence, the key
               we seek can't be in the hash table. */
            /* Strange. Not in the hash. Should this be an oops? */
            return 0;
        }
        ls.probe_distance += ls.metadata_increment;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= (control->max_probe_distance + 1) * ls.metadata_increment);
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + MVM_ptr_hash_max_items(control));
        assert(ls.metadata < MVM_ptr_hash_metadata(control) + MVM_ptr_hash_official_size(control) + 256);
    }
}
