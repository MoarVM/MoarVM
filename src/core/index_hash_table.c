#include "moar.h"

#define INDEX_MIN_SIZE_BASE_2 3

MVM_STATIC_INLINE void hash_demolish_internal(MVMThreadContext *tc,
                                              struct MVMIndexHashTableControl *control) {
    size_t allocated_items = MVM_index_hash_allocated_items(control);
    size_t entries_size = MVM_hash_round_size_up(sizeof(struct MVMIndexHashEntry) * allocated_items);
    char *start = (char *)control - entries_size;
    MVM_free(start);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_index_hash_demolish(MVMThreadContext *tc, MVMIndexHashTable *hashtable) {
    struct MVMIndexHashTableControl *control = hashtable->table;
    if (!control)
        return;
    hash_demolish_internal(tc, control);
    hashtable->table = NULL;
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE struct MVMIndexHashTableControl *hash_allocate_common(MVMThreadContext *tc,
                                                                        MVMuint8 key_right_shift,
                                                                        MVMuint8 official_size_log2) {
    MVMuint32 official_size = 1 << (MVMuint32)official_size_log2;
    MVMuint32 max_items = official_size * MVM_INDEX_HASH_LOAD_FACTOR;
    MVMuint8 max_probe_distance_limit;
    if (MVM_HASH_MAX_PROBE_DISTANCE < max_items) {
        max_probe_distance_limit = MVM_HASH_MAX_PROBE_DISTANCE;
    } else {
        max_probe_distance_limit = max_items;
    }
    size_t allocated_items = official_size + max_probe_distance_limit - 1;
    /* MVM_index_hash is the only variant that needs this round up here, because
     * it's the only variant where the entry size is not a multiple of pointers/
     * unsigned longs.
     *
     * (It's always 32 bit, so on 64 bit systems we might want to allocate an
     * odd number of entries, and without this we'd end up with control and then
     * *metadata* not unsigned long aligned. Which breaks alignment assumptions
     * in maybe_grow_hash, on platforms where this matters. Certainly sparc64,
     * but possibly also x86_64 if the compiler decides that the code in the
     * loop can be compiled to vector instructions.) */
    size_t entries_size = MVM_hash_round_size_up(sizeof(struct MVMIndexHashEntry) * allocated_items);
    size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
    size_t total_size
        = entries_size + sizeof(struct MVMIndexHashTableControl) + metadata_size;
    assert(total_size == MVM_hash_round_size_up(total_size));

    struct MVMIndexHashTableControl *control =
        (struct MVMIndexHashTableControl *) ((char *)MVM_malloc(total_size) + entries_size);

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

void MVM_index_hash_build(MVMThreadContext *tc,
                          MVMIndexHashTable *hashtable,
                          MVMuint32 entries) {
    MVMuint32 initial_size_base2;
    if (!entries) {
        initial_size_base2 = INDEX_MIN_SIZE_BASE_2;
    } else {
        /* Minimum size we need to allocate, given the load factor. */
        MVMuint32 min_needed = entries * (1.0 / MVM_INDEX_HASH_LOAD_FACTOR);
        initial_size_base2 = MVM_round_up_log_base2(min_needed);
        if (initial_size_base2 < INDEX_MIN_SIZE_BASE_2) {
            /* "Too small" - use our original defaults. */
            initial_size_base2 = INDEX_MIN_SIZE_BASE_2;
        }
    }

    hashtable->table = hash_allocate_common(tc,
                                            (8 * sizeof(MVMuint64) - initial_size_base2),
                                            initial_size_base2);
}

MVM_STATIC_INLINE void hash_insert_internal(MVMThreadContext *tc,
                                            struct MVMIndexHashTableControl *control,
                                            MVMString **list,
                                            MVMuint32 idx) {
    if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %i",
                 idx);
    }

    struct MVM_hash_loop_state ls = MVM_index_hash_create_loop_state(tc, control, list[idx]);

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
                 * memmove(entry_raw + sizeof(struct MVMIndexHashEntry), entry_raw,
                 *         sizeof(struct MVMIndexHashEntry) * entries_to_move);
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
            struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) ls.entry_raw;
            entry->index = idx;

            return;
        }
        else if (*ls.metadata == ls.probe_distance) {
            struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) ls.entry_raw;
            if (entry->index == idx) {
                /* definately XXX - what should we do here? */
                MVM_oops(tc, "insert duplicate for %u", idx);
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
        assert(ls.metadata < MVM_index_hash_metadata(control) + MVM_index_hash_official_size(control) + control->max_items);
        assert(ls.metadata < MVM_index_hash_metadata(control) + MVM_index_hash_official_size(control) + 256);
    }
}

static struct MVMIndexHashTableControl *maybe_grow_hash(MVMThreadContext *tc,
                                                        struct MVMIndexHashTableControl *control,
                                                        MVMString **list) {
    /* control->max_items may have been set to 0 to trigger a call into this
     * function. */
    MVMuint32 max_items = MVM_index_hash_max_items(control);
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

        MVMuint8 *metadata = MVM_index_hash_metadata(control);
        MVMuint32 in_use_items = MVM_index_hash_official_size(control) + max_probe_distance;
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

    MVMuint32 entries_in_use =  MVM_index_hash_kompromat(control);
    MVMuint8 *entry_raw_orig = MVM_index_hash_entries(control);
    MVMuint8 *metadata_orig = MVM_index_hash_metadata(control);

    struct MVMIndexHashTableControl *control_orig = control;

    control = hash_allocate_common(tc,
                                   control_orig->key_right_shift - 1,
                                   control_orig->official_size_log2 + 1);

    MVMuint8 *entry_raw = entry_raw_orig;
    MVMuint8 *metadata = metadata_orig;
    MVMHashNumItems bucket = 0;
    while (bucket < entries_in_use) {
        if (*metadata) {
            struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) entry_raw;
            hash_insert_internal(tc, control, list, entry->index);

            if (!control->max_items) {
                /* Probably we hit the probe limit.
                 * But it's just possible that one actual "grow" wasn't enough.
                 */
                struct MVMIndexHashTableControl *new_control
                    = maybe_grow_hash(tc, control, list);
                if (new_control) {
                    control = new_control;
                }
            }
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(struct MVMIndexHashEntry);
    }
    hash_demolish_internal(tc, control_orig);
    return control;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_index_hash_insert_nocheck(MVMThreadContext *tc,
                                   MVMIndexHashTable *hashtable,
                                   MVMString **list,
                                   MVMuint32 idx) {
    struct MVMIndexHashTableControl *control = hashtable->table;
    assert(control);
    assert(MVM_index_hash_entries(control) != NULL);
    if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        struct MVMIndexHashTableControl *new_control = maybe_grow_hash(tc, control, list);
        if (new_control) {
            /* We could unconditionally assign this, but that would mean CPU
             * cache writes even when it was unchanged, and the address of
             * hashtable will not be in the same cache lines as we are writing
             * for the hash internals, so it will churn the write cache. */
            hashtable->table = control = new_control;
        }
    }
    return hash_insert_internal(tc, control, list, idx);
}
