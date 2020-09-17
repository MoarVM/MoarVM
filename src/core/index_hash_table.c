#include "moar.h"

#define INDEX_LOAD_FACTOR 0.75
#define INDEX_MIN_SIZE_BASE_2 3

MVM_STATIC_INLINE MVMuint32 hash_true_size(MVMIndexHashTable *hashtable) {
    MVMuint32 true_size = hashtable->official_size + hashtable->max_items - 1;
    if (hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE < true_size) {
        true_size = hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE;
    }
    return true_size;
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_index_hash_demolish(MVMThreadContext *tc, MVMIndexHashTable *hashtable) {
    if (hashtable->entries) {
        MVM_free(hashtable->entries
                 - sizeof(struct MVMIndexHashEntry) * (hash_true_size(hashtable) - 1));
    }
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE void hash_allocate_common(MVMIndexHashTable *hashtable) {
    hashtable->max_items = hashtable->official_size * INDEX_LOAD_FACTOR;
    size_t actual_items = hash_true_size(hashtable);
    size_t entries_size = sizeof(struct MVMIndexHashEntry) * actual_items;
    size_t metadata_size = 1 + actual_items + 1;
    hashtable->metadata
        = (MVMuint8 *) MVM_malloc(entries_size + metadata_size) + entries_size;
    memset(hashtable->metadata, 0, metadata_size);
    /* We point to the *last* entry in the array, not the one-after-the end. */
    hashtable->entries = hashtable->metadata - sizeof(struct MVMIndexHashEntry);
    /* A sentinel. This marks an occupied slot, at its ideal position. */
    *hashtable->metadata = 1;
    ++hashtable->metadata;
    /* A sentinel at the other end. Again, occupied, ideal position. */
    hashtable->metadata[actual_items] = 1;
}

void MVM_index_hash_build(MVMThreadContext *tc,
                          MVMIndexHashTable *hashtable,
                          MVMuint32 entries) {
    memset(hashtable, 0, sizeof(*hashtable));

    MVMuint32 initial_size_base2;
    if (!entries) {
        initial_size_base2 = INDEX_MIN_SIZE_BASE_2;
    } else {
        /* Minimum size we need to allocate, given the load factor. */
        MVMuint32 min_needed = entries * (1.0 / INDEX_LOAD_FACTOR);
        initial_size_base2 = MVM_round_up_log_base2(min_needed);
        if (initial_size_base2 < INDEX_MIN_SIZE_BASE_2) {
            /* "Too small" - use our original defaults. */
            initial_size_base2 = INDEX_MIN_SIZE_BASE_2;
        }
    }

    hashtable->key_right_shift = (8 * sizeof(MVMuint64) - initial_size_base2);
    hashtable->official_size = 1 << initial_size_base2;

    hash_allocate_common(hashtable);
}

/* make sure you still have your copies of entries and metadata before you
   call this. */
MVM_STATIC_INLINE void hash_grow(MVMIndexHashTable *hashtable) {
    --hashtable->key_right_shift;
    hashtable->official_size *= 2;

    hash_allocate_common(hashtable);
}

MVM_STATIC_INLINE void hash_insert_internal(MVMThreadContext *tc,
                                            MVMIndexHashTable *hashtable,
                                            MVMString **list,
                                            MVMuint32 idx) {
    if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %i",
                 idx);
    }

    unsigned int probe_distance = 1;
    MVMuint64 hash_val = MVM_string_hash_code(tc, list[idx]);
    MVMHashNumItems bucket = hash_val >> hashtable->key_right_shift;
    MVMuint8 *entry_raw = hashtable->entries - bucket * sizeof(struct MVMIndexHashEntry);
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
                size_t size_to_move = sizeof(struct MVMIndexHashEntry) * entries_to_move;
                /* When we had entries *ascending* this was
                 * memmove(entry_raw + sizeof(struct MVMIndexHashEntry), entry_raw,
                 *         sizeof(struct MVMIndexHashEntry) * entries_to_move);
                 * because we point to the *start* of the block of memory we
                 * want to move, and we want to move it one "entry" forwards.
                 * `entry_raw` is still a pointer to where we want to make free
                 * space, but what want to do now is move everything at it and
                 * *before* it downwards. */
                MVMuint8 *dest = entry_raw - size_to_move;
                memmove(dest, dest + sizeof(struct MVMIndexHashEntry), size_to_move);
            }
            
            /* The same test and optimisation as in the "make room" loop - we're
             * about to insert something at the (current) max_probe_distance, so
             * signal to the next insertion that it needs to take action first.
             */
            if (probe_distance == MVM_HASH_MAX_PROBE_DISTANCE) {
                hashtable->max_items = 0;
            }

            *metadata = probe_distance;
            struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) entry_raw;
            entry->index = idx;

            return;
        }

        if (*metadata == probe_distance) {
            struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) entry_raw;
            if (entry->index == idx) {
                /* definately XXX - what should we do here? */
                MVM_oops(tc, "insert duplicate for %u", idx);
            }
        }
        ++probe_distance;
        ++metadata;
        entry_raw -= sizeof(struct MVMIndexHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_index_hash_insert_nocheck(MVMThreadContext *tc,
                                   MVMIndexHashTable *hashtable,
                                   MVMString **list,
                                   MVMuint32 idx) {
    assert(hashtable->entries != NULL);
    if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVMuint32 true_size =  hash_true_size(hashtable);
        MVMuint8 *entry_raw_orig = hashtable->entries;
        MVMuint8 *metadata_orig = hashtable->metadata;

        hash_grow(hashtable);

        MVMuint8 *entry_raw = entry_raw_orig;
        MVMuint8 *metadata = metadata_orig;
        MVMHashNumItems bucket = 0;
        while (bucket < true_size) {
            if (*metadata) {
                struct MVMIndexHashEntry *entry = (struct MVMIndexHashEntry *) entry_raw;
                hash_insert_internal(tc, hashtable, list, entry->index);
            }
            ++bucket;
            ++metadata;
            entry_raw -= sizeof(struct MVMIndexHashEntry);
        }
        MVM_free(entry_raw_orig - sizeof(struct MVMIndexHashEntry) * (true_size - 1));
    }
    hash_insert_internal(tc, hashtable, list, idx);
    ++hashtable->cur_items;
}
