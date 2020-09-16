#include "moar.h"

#define FIXKEY_LOAD_FACTOR 0.75
#define FIXKEY_INITIAL_SIZE 8
#define FIXKEY_INITIAL_KEY_RIGHT_SHIFT (8 * sizeof(MVMuint64) - 3)

MVM_STATIC_INLINE MVMuint32 hash_true_size(MVMFixKeyHashTable *hashtable) {
    MVMuint32 true_size = hashtable->official_size + hashtable->max_items - 1;
    if (hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE < true_size) {
        true_size = hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE;
    }
    return true_size;
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_fixkey_hash_demolish(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable) {
    MVMuint32 true_size = hash_true_size(hashtable);
    char *entry_raw = hashtable->entries;
    MVMuint8 *metadata = hashtable->metadata;
    MVMuint32 bucket = 0;
    while (bucket < true_size) {
        if (*metadata) {
            MVMString ***indirection = (MVMString ***) entry_raw;
            MVM_fixed_size_free(tc, tc->instance->fsa, hashtable->entry_size, *indirection);
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(MVMString ***);
    }

    if (hashtable->entries) {
        MVM_free(hashtable->entries
             - sizeof(MVMString ***) * (hash_true_size(hashtable) - 1));
        MVM_free(metadata - 1);
    }
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE void hash_allocate_common(MVMFixKeyHashTable *hashtable) {
    hashtable->max_items = hashtable->official_size * FIXKEY_LOAD_FACTOR;
    size_t actual_items = hash_true_size(hashtable);
    /* We point to the *last* entry in the array, not the one-after-the end. */
    hashtable->entries = (char *) MVM_malloc(hashtable->entry_size * actual_items)
        + sizeof(MVMString ***) * (actual_items - 1);
    hashtable->metadata = MVM_calloc(1 + actual_items + 1, 1);
    /* A sentinel. This marks an occupied slot, at its ideal position. */
    *hashtable->metadata = 1;
    ++hashtable->metadata;
    /* A sentinel at the other end. Again, occupied, ideal position. */
    hashtable->metadata[actual_items] = 1;
}

MVM_STATIC_INLINE void hash_initial_allocate(MVMFixKeyHashTable *hashtable) {
    hashtable->key_right_shift = FIXKEY_INITIAL_KEY_RIGHT_SHIFT;
    hashtable->official_size = FIXKEY_INITIAL_SIZE;

    hash_allocate_common(hashtable);
}

/* make sure you still have your copies of entries and metadata before you
   call this. */
MVM_STATIC_INLINE void hash_grow(MVMFixKeyHashTable *hashtable) {
    --hashtable->key_right_shift;
    hashtable->official_size *= 2;

    hash_allocate_common(hashtable);
}

MVM_STATIC_INLINE MVMString ***hash_insert_internal(MVMThreadContext *tc,
                                                    MVMFixKeyHashTable *hashtable,
                                                    MVMString *key) {
    if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %p",
                 key);
    }

    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_fixkey_hash_code(tc, key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries - bucket * sizeof(MVMString ***);
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
                size_t size_to_move = sizeof(MVMString ***) * entries_to_move;
                /* When we had entries *ascending* this was
                 * memmove(entry_raw + sizeof(MVMString ***), entry_raw,
                 *         sizeof(MVMString ***) * entries_to_move);
                 * because we point to the *start* of the block of memory we
                 * want to move, and we want to move it one "entry" forwards.
                 * `entry_raw` is still a pointer to where we want to make free
                 * space, but what want to do now is move everything at it and
                 * *before* it downwards. */
                char *dest = entry_raw - size_to_move;
                memmove(dest, dest + sizeof(MVMString ***), size_to_move);
            }

            /* The same test and optimisation as in the "make room" loop - we're
             * about to insert something at the (current) max_probe_distance, so
             * signal to the next insertion that it needs to take action first.
             */
            if (probe_distance == MVM_HASH_MAX_PROBE_DISTANCE) {
                hashtable->max_items = 0;
            }

            *metadata = probe_distance;
            MVMString ***indirection = (MVMString ***) entry_raw;
            /* Set this to NULL to signal that this has just been allocated. */
            *indirection = NULL;
            return indirection;
        }

        if (*metadata == probe_distance) {
            MVMString ***indirection = (MVMString ***) entry_raw;
            MVMString **entry = *indirection;
            if (*entry == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, *entry)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           *entry, 0))) {
                return indirection;
            }
        }
        ++probe_distance;
        ++metadata;
        entry_raw -= sizeof(MVMString ***);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

/* Oh, fsck, I needed to implement this: */
MVMuint64 MVM_fixkey_hash_fsck(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable, MVMuint32 mode);

void *MVM_fixkey_hash_lvalue_fetch_nocheck(MVMThreadContext *tc,
                                           MVMFixKeyHashTable *hashtable,
                                           MVMString *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        if (MVM_UNLIKELY(hashtable->entry_size == 0)) {
            /* This isn't going to work, because we'll allocate 0 bytes from the
             * FSA, but then try to write a pointer into it. */
            MVM_oops(tc, "Attempting insert on MVM_fixkey_hash without setting entry_size");
        }
        hash_initial_allocate(hashtable);
    }
    else if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        /* We should avoid growing the hash if we don't need to.
         * It's expensive, and for hashes with iterators, growing the hash
         * invalidates iterators. Which is buggy behaviour if the fetch doesn't
         * need to create a key. */
        MVMString **entry = MVM_fixkey_hash_fetch_nocheck(tc, hashtable, key);
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
                /* We need to "move" the pointer to entry from the old flat
                 * storage array storage to the new flat storage array.
                 * entry remains as was - that's the whole point of this
                 * hashtable variant - to keep the user-visible structure at a
                 * fixed address. */
                MVMString ***old_indirection = (MVMString ***) entry_raw;
                MVMString **entry = *old_indirection;
                MVMString ***new_indirection =
                    hash_insert_internal(tc, hashtable, *entry);
                if(*new_indirection) {
                    char *wrong = MVM_string_utf8_encode_C_string(tc, key);
                    MVM_oops(tc, "new_indrection was not NULL in MVM_fixkey_hash_lvalue_fetch_nocheck when adding key %s", wrong);
                } else {
                    *new_indirection = *old_indirection;
                }
            }
            ++bucket;
            ++metadata;
            entry_raw -= sizeof(MVMString ***);
        }
        MVM_free(entry_raw_orig - sizeof(MVMString ***) * (true_size - 1));
        MVM_free(metadata_orig - 1);
    }
    MVMString ***indirection = hash_insert_internal(tc, hashtable, key);
    if (!*indirection) {
        ++hashtable->cur_items;
        MVMString **entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, hashtable->entry_size);
        /* and we then set *this* to NULL to signal to our caller that this is a
         * new allocation. */
        *entry = NULL;
        *indirection = entry;
    }
    return *indirection;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care.
 * (well that's the official line. As you can see, the oops suggests we
 * currently don't exploit the documented freedom.) */
void *MVM_fixkey_hash_insert_nocheck(MVMThreadContext *tc,
                                     MVMFixKeyHashTable *hashtable,
                                     MVMString *key) {
    MVMString **new_entry = MVM_fixkey_hash_lvalue_fetch_nocheck(tc, hashtable, key);
    if (*new_entry) {
        /* This footgun has a safety catch. */
        MVM_oops(tc, "duplicate key in fixkey_hash_insert");
    } else {
        *new_entry = key;
    }
    return new_entry;
}

/* This is not part of the public API, and subject to change at any point.
   (possibly in ways that are actually incompatible but won't generate compiler
   warnings.) */
MVMuint64 MVM_fixkey_hash_fsck(MVMThreadContext *tc, MVMFixKeyHashTable *hashtable, MVMuint32 mode) {
    const char *prefix_hashes = mode & 1 ? "# " : "";
    MVMuint32 display = (mode >> 1) & 3;
    MVMuint64 errors = 0;
    MVMuint64 seen = 0;

    if (hashtable->entries == NULL) {
        return 0;
    }

    MVMuint32 true_size = hash_true_size(hashtable);
    char *entry_raw = hashtable->entries;
    MVMuint8 *metadata = hashtable->metadata;
    MVMuint32 bucket = 0;
    MVMint64 prev_offset = 0;
    while (bucket < true_size) {
        if (!*metadata) {
            /* empty slot. */
            prev_offset = 0;
            if (display == 2) {
                fprintf(stderr, "%s%3X\n", prefix_hashes, bucket);
            }
        } else {
            ++seen;

            MVMString ***indirection = (MVMString ***) entry_raw;
            if (!indirection) {
                ++errors;
                fprintf(stderr, "%s%3X!\n", prefix_hashes, bucket);
            } else {
                MVMString **entry = *indirection;
                if (!entry) {
                    ++errors;
                    fprintf(stderr, "%s%3X!!\n", prefix_hashes, bucket);
                } else {
                    MVMuint64 hash_val = MVM_fixkey_hash_code(tc, *entry);
                    MVMuint32 ideal_bucket = hash_val >> hashtable->key_right_shift;
                    MVMint64 offset = 1 + bucket - ideal_bucket;
                    int wrong_bucket = offset != *metadata;
                    int wrong_order = offset < 1 || offset > prev_offset + 1;

                    if (display == 2 || wrong_bucket || wrong_order) {
                        MVMuint64 len = MVM_string_graphs(tc, *entry);
                        char *key = MVM_string_utf8_encode_C_string(tc, *entry);
                        fprintf(stderr, "%s%3X%c%3"PRIx64"%c%0"PRIx64" (%"PRIu64") %s\n", prefix_hashes,
                                bucket, wrong_bucket ? '!' : ' ', offset,
                                wrong_order ? '!' : ' ', hash_val, len, key);
                        errors += wrong_bucket + wrong_order;
                    }
                    prev_offset = offset;
                }
            }
        }
        ++bucket;
        ++metadata;
        entry_raw -= sizeof(MVMString ***);
    }
    if (*metadata != 1) {
        ++errors;
        if (display) {
            fprintf(stderr, "%s    %02x!\n", prefix_hashes, *metadata);
        }
    }
    if (seen != hashtable->cur_items) {
        ++errors;
        if (display) {
            fprintf(stderr, "%s %"PRIx64"u != %"PRIx32"u \n",
                    prefix_hashes, seen, hashtable->cur_items);
        }
    }

    return errors;
}
