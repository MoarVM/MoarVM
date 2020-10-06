#include "moar.h"

#define UNI_MIN_SIZE_BASE_2 3

MVM_STATIC_INLINE void hash_demolish_internal(MVMThreadContext *tc,
                                              struct MVMUniHashTableControl *control) {
    size_t allocated_items = MVM_uni_hash_allocated_items(control);
    size_t entries_size = sizeof(struct MVMUniHashEntry) * allocated_items;
    char *start = (char *)control - entries_size;
    MVM_free(start);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_uni_hash_demolish(MVMThreadContext *tc, MVMUniHashTable *hashtable) {
    struct MVMUniHashTableControl *control = hashtable->table;
    if (!control)
        return;
    hash_demolish_internal(tc, control);
    hashtable->table = NULL;
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE struct MVMUniHashTableControl *hash_allocate_common(MVMThreadContext *tc,
                                                                      MVMuint8 key_right_shift,
                                                                      MVMuint8 official_size_log2) {
    MVMuint32 official_size = 1 << (MVMuint32)official_size_log2;
    MVMuint32 max_items = official_size * MVM_UNI_HASH_LOAD_FACTOR;
    MVMuint8 max_probe_distance_limit;
    if (MVM_HASH_MAX_PROBE_DISTANCE < max_items) {
        max_probe_distance_limit = MVM_HASH_MAX_PROBE_DISTANCE;
    } else {
        max_probe_distance_limit = max_items;
    }
    size_t allocated_items = official_size + max_probe_distance_limit - 1;
    size_t entries_size = sizeof(struct MVMUniHashEntry) * allocated_items;
    size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
    size_t total_size
        = entries_size + sizeof(struct MVMUniHashTableControl) + metadata_size;

    struct MVMUniHashTableControl *control =
        (struct MVMUniHashTableControl *) ((char *)MVM_malloc(total_size) + entries_size);

    control->official_size_log2 = official_size_log2;
    control->max_items = max_items;
    control->cur_items = 0;
    control->metadata_hash_bits = 0;
    MVMuint8 initial_probe_distance = (1 << (8 - MVM_HASH_INITIAL_BITS_IN_METADATA)) - 1;
    control->max_probe_distance = max_probe_distance_limit > initial_probe_distance ? initial_probe_distance : max_probe_distance_limit;
    control->max_probe_distance_limit = max_probe_distance_limit;
    control->key_right_shift = key_right_shift;

    MVMuint8 *metadata = (MVMuint8 *)(control + 1);
    memset(metadata, 0, metadata_size);

    /* A sentinel. This marks an occupied slot, at its ideal position. */
    metadata[allocated_items] = 1;

    return control;
}

void MVM_uni_hash_build(MVMThreadContext *tc,
                        MVMUniHashTable *hashtable,
                        MVMuint32 entries) {
    MVMuint32 initial_size_base2;
    if (!entries) {
        initial_size_base2 = UNI_MIN_SIZE_BASE_2;
    } else {
        /* Minimum size we need to allocate, given the load factor. */
        MVMuint32 min_needed = entries * (1.0 / MVM_UNI_HASH_LOAD_FACTOR);
        initial_size_base2 = MVM_round_up_log_base2(min_needed);
        if (initial_size_base2 < UNI_MIN_SIZE_BASE_2) {
            /* "Too small" - use our original defaults. */
            initial_size_base2 = UNI_MIN_SIZE_BASE_2;
        }
    }

    hashtable->table = hash_allocate_common(tc,
                                            (8 * sizeof(MVMuint32) - initial_size_base2),
                                            initial_size_base2);
}

static MVMuint64 uni_hash_fsck_internal(struct MVMUniHashTableControl *control, MVMuint32 mode);

MVM_STATIC_INLINE struct MVMUniHashEntry *hash_insert_internal(MVMThreadContext *tc,
                                                               struct MVMUniHashTableControl *control,
                                                               const char *key,
                                                               MVMuint32 hash_val) {
    if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        uni_hash_fsck_internal(control, 5);
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %s",
                 key);
    }

    struct MVM_hash_loop_state ls = MVM_uni_hash_create_loop_state(control,
                                                                   hash_val);

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
                 * memmove(entry_raw + sizeof(struct MVMUniHashEntry), entry_raw,
                 *         sizeof(struct MVMUniHashEntry) * entries_to_move);
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
            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) ls.entry_raw;
            entry->key = NULL;
            entry->hash_val = hash_val;
            return entry;
        }

        if (*ls.metadata == ls.probe_distance) {
            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) ls.entry_raw;
            if (entry->hash_val == hash_val && 0 == strcmp(entry->key, key)) {
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
         * beyond the maximum probe distance (hence +1 in the seemingly
         * analogous assertions) - but if so, it will always read from the
         * metadata a probe distance which is lower than the current probe
         * distance, and hence hit "not found" and terminate the loop.
         *
         * This is how the loop terminates when the max probe distance is
         * reached without needing an explicit test for it, and why we need an
         * initialised sentinel byte at the end of the metadata. */

        assert(ls.probe_distance <= ls.max_probe_distance * ls.metadata_increment);
        assert(ls.metadata < MVM_uni_hash_metadata(control) + MVM_uni_hash_official_size(control) + MVM_uni_hash_max_items(control));
        assert(ls.metadata < MVM_uni_hash_metadata(control) + MVM_uni_hash_official_size(control) + 256);
    }
}

static struct MVMUniHashTableControl *maybe_grow_hash(MVMThreadContext *tc,
                                                      struct MVMUniHashTableControl *control) {
    /* control->max_items may have been set to 0 to trigger a call into this
     * function. */
    MVMuint32 max_items = MVM_uni_hash_max_items(control);
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

        control->max_probe_distance = new_probe_distance;
        /* Reset this to its proper value. */
        control->max_items = max_items;
        assert(control->max_items);
        return NULL;
    }

    MVMuint32 entries_in_use = MVM_uni_hash_official_size(control) + control->max_probe_distance - 1;
    MVMuint8 *entry_raw_orig = MVM_uni_hash_entries(control);
    MVMuint8 *metadata_orig = MVM_uni_hash_metadata(control);

    struct MVMUniHashTableControl *control_orig = control;

    control = hash_allocate_common(tc,
                                   control_orig->key_right_shift - 1,
                                   control_orig->official_size_log2 + 1);

    MVMuint8 *entry_raw = entry_raw_orig;
    MVMuint8 *metadata = metadata_orig;
    MVMHashNumItems bucket = 0;
    while (bucket < entries_in_use) {
        if (*metadata) {
            struct MVMUniHashEntry *old_entry = (struct MVMUniHashEntry *) entry_raw;
            struct MVMUniHashEntry *new_entry =
                hash_insert_internal(tc, control, old_entry->key, old_entry->hash_val);
            assert(new_entry->key == NULL);
            *new_entry = *old_entry;

            if (!control->max_items) {
                /* Probably we hit the probe limit.
                 * But it's just possible that one actual "grow" wasn't enough.
                 */
                struct MVMUniHashTableControl *new_control
                    = maybe_grow_hash(tc, control);
                if (new_control) {
                    control = new_control;
                }
            }
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(struct MVMUniHashEntry);
    }
    hash_demolish_internal(tc, control_orig);
    return control;
}

/* I think that we are going to expose this soon. */
MVM_STATIC_INLINE void *MVM_uni_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMUniHashTable *hashtable,
                                                  const char *key) {
    struct MVMUniHashTableControl *control = hashtable->table;
    if (!control) {
        MVM_uni_hash_build(tc, hashtable, 0);
        control = hashtable->table;
    }
    else if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        /* We should avoid growing the hash if we don't need to.
         * It's expensive, and for hashes with iterators, growing the hash
         * invalidates iterators. Which is buggy behaviour if the fetch doesn't
         * need to create a key. */
        struct MVMUniHashEntry *entry = MVM_uni_hash_fetch(tc, hashtable, key);
        if (entry) {
            return entry;
        }
        struct MVMUniHashTableControl *new_control = maybe_grow_hash(tc, control);
        if (new_control) {
            /* We could unconditionally assign this, but that would mean CPU
             * cache writes even when it was unchanged, and the address of
             * hashtable will not be in the same cache lines as we are writing
             * for the hash internals, so it will churn the write cache. */
            hashtable->table = control = new_control;
        }
    }
    MVMuint32 hash_val = MVM_uni_hash_code(key, strlen(key));
    return hash_insert_internal(tc, control, key, hash_val);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care.
 * (well that's the official line. As you can see, the XXX suggests we currently
 * don't exploit the documented freedom.) */
void MVM_uni_hash_insert(MVMThreadContext *tc,
                         MVMUniHashTable *hashtable,
                         const char *key,
                         MVMint32 value) {
    struct MVMUniHashEntry *new_entry = MVM_uni_hash_lvalue_fetch(tc, hashtable, key);
    if (new_entry->key) {
        if (value != new_entry->value) {
            MVMuint32 hash_val = MVM_uni_hash_code(key, strlen(key));

            /* definately XXX - what should we do here? */
            MVM_oops(tc, "insert conflict, %s is %u, %i != %i", key, hash_val, value, new_entry->value);
        }
    } else {
        new_entry->key = key;
        new_entry->value = value;
    }
}

/* This is not part of the public API, and subject to change at any point.
   (possibly in ways that are actually incompatible but won't generate compiler
   warnings.) */
MVMuint64 MVM_uni_hash_fsck(MVMUniHashTable *hashtable, MVMuint32 mode) {
    return uni_hash_fsck_internal(hashtable->table, mode);
}

static MVMuint64 uni_hash_fsck_internal(struct MVMUniHashTableControl *control, MVMuint32 mode) {
    const char *prefix_hashes = mode & 1 ? "# " : "";
    MVMuint32 display = (mode >> 1) & 3;
    MVMuint64 errors = 0;
    MVMuint64 seen = 0;

    if (control == NULL) {
        return 0;
    }

    MVMuint32 allocated_items = MVM_uni_hash_allocated_items(control);
    const MVMuint8 metadata_hash_bits = control->metadata_hash_bits;
    MVMuint8 *entry_raw = MVM_uni_hash_entries(control);
    MVMuint8 *metadata = MVM_uni_hash_metadata(control);
    MVMuint32 bucket = 0;
    MVMint64 prev_offset = 0;
    while (bucket < allocated_items) {
        if (!*metadata) {
            /* empty slot. */
            prev_offset = 0;
            if (display == 2) {
                fprintf(stderr, "%s%3X\n", prefix_hashes, bucket);
            }
        } else {
            ++seen;

            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
            MVMuint32 ideal_bucket = entry->hash_val >> control->key_right_shift;
            MVMint64 offset = 1 + bucket - ideal_bucket;
            MVMuint32 actual_bucket = *metadata >> metadata_hash_bits;
            char wrong_bucket = offset == actual_bucket ? ' ' : '!';
            char wrong_order;
            if (offset < 1) {
                wrong_order = '<';
            } else if (offset > control->max_probe_distance) {
                ++errors;
                wrong_order = '>';
            } else if (offset > prev_offset + 1) {
                ++errors;
                wrong_order = '!';
            } else {
                wrong_order = ' ';
            }
            int error_count = (wrong_bucket != ' ') + (wrong_order != ' ');

            if (display == 2 || error_count) {
                fprintf(stderr, "%s%3X%c%3"PRIx64"%c%08X %s\n", prefix_hashes,
                        bucket, wrong_bucket, offset,
                        wrong_order, entry->hash_val, entry->key);
                errors += error_count;
            }
            prev_offset = offset;
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(struct MVMUniHashEntry);
    }
    if (*metadata != 1) {
        ++errors;
        if (display) {
            fprintf(stderr, "%s    %02x!\n", prefix_hashes, *metadata);
        }
    }
    if (seen != control->cur_items) {
        ++errors;
        if (display) {
            fprintf(stderr, "%s %"PRIx64"u != %"PRIx32"u \n",
                    prefix_hashes, seen, control->cur_items);
        }
    }

    return errors;
}
