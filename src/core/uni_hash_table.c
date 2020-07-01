#include "moar.h"

#define UNI_LOAD_FACTOR 0.75
#define UNI_INITIAL_SIZE 8
#define UNI_INITIAL_KEY_RIGHT_SHIFT (8 * sizeof(MVMuint32) - 3)

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_uni_hash_demolish(MVMThreadContext *tc, MVMUniHashTable *hashtable) {
    free(hashtable->entries);
    free(hashtable->metadata);
}
/* and then free memory if you allocated it */


MVM_STATIC_INLINE MVMuint32 hash_true_size(MVMUniHashTable *hashtable) {
    MVMuint32 true_size = hashtable->official_size + hashtable->max_items - 1;
    if (hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE < true_size) {
        true_size = hashtable->official_size + MVM_HASH_MAX_PROBE_DISTANCE;
    }
    return true_size;
}

MVM_STATIC_INLINE void hash_allocate_common(MVMUniHashTable *hashtable) {
    hashtable->max_items = hashtable->official_size * UNI_LOAD_FACTOR;
    size_t actual_items = hash_true_size(hashtable);
    hashtable->entries = malloc(sizeof(struct MVMUniHashEntry) * actual_items);
    hashtable->metadata = calloc(actual_items + 1, 1);
    /* A sentinel. This marks an occupied slot, at its ideal position. */
    hashtable->metadata[actual_items] = 1;
}

MVM_STATIC_INLINE void hash_initial_allocate(MVMUniHashTable *hashtable) {
    hashtable->key_right_shift = UNI_INITIAL_KEY_RIGHT_SHIFT;
    hashtable->official_size = UNI_INITIAL_SIZE;

    hash_allocate_common(hashtable);
}

/* make sure you still have your copies of entries and metadata before you
   call this. */
MVM_STATIC_INLINE void hash_grow(MVMUniHashTable *hashtable) {
    --hashtable->key_right_shift;
    hashtable->official_size *= 2;

    hash_allocate_common(hashtable);
}

MVMuint64 MVM_uni_hash_fsck(MVMUniHashTable *hashtable, MVMuint32 mode);

MVM_STATIC_INLINE void hash_insert_internal(MVMThreadContext *tc,
                                            MVMUniHashTable *hashtable,
                                            const char *key,
                                            MVMuint32 hash_val,
                                            MVMint32 value) {
    if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVM_uni_hash_fsck(hashtable, 5);
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %s => %d",
                 key, value);
    }

    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = hash_val >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries + bucket * sizeof(struct MVMUniHashEntry);
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
                memmove(entry_raw + sizeof(struct MVMUniHashEntry), entry_raw,
                        sizeof(struct MVMUniHashEntry) * entries_to_move);
            }

            *metadata = probe_distance;
            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
            entry->key = key;
            entry->hash_val = hash_val;
            entry->value = value;

            return;
        }

        if (*metadata == probe_distance) {
            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
            if (entry->hash_val == hash_val && 0 == strcmp(entry->key, key)) {
                if (value != entry->value) {
                    /* definately XXX - what should we do here? */
                    MVM_oops(tc, "insert conflict, %s is %u, %i != %i", key, hash_val, value, entry->value);
                }
                return;
            }
        }
        ++probe_distance;
        ++metadata;
        entry_raw += sizeof(struct MVMUniHashEntry);
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void MVM_uni_hash_insert(MVMThreadContext *tc,
                         MVMUniHashTable *hashtable,
                         const char *key,
                         MVMint32 value) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        hash_initial_allocate(hashtable);
    }
    else if (MVM_UNLIKELY(hashtable->cur_items >= hashtable->max_items)) {
        MVMuint32 true_size =  hash_true_size(hashtable);
        char *entry_raw_orig = hashtable->entries;
        MVMuint8 *metadata_orig = hashtable->metadata;

        hash_grow(hashtable);

        char *entry_raw = entry_raw_orig;
        MVMuint8 *metadata = metadata_orig;
        MVMHashNumItems bucket = 0;
        while (bucket < true_size) {
            if (*metadata) {
                struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
                hash_insert_internal(tc, hashtable, entry->key, entry->hash_val, entry->value);
            }
            ++bucket;
            ++metadata;
            entry_raw += sizeof(struct MVMUniHashEntry);
        }
        free(entry_raw_orig);
        free(metadata_orig);
    }
    MVMuint32 hash_val = MVM_uni_hash_code(key, strlen(key));
    hash_insert_internal(tc, hashtable, key, hash_val, value);
    ++hashtable->cur_items;
}

/* This is not part of the public API, and subject to change at any point.
   (possibly in ways that are actually incompatible but won't generate compiler
   warnings.) */
MVMuint64 MVM_uni_hash_fsck(MVMUniHashTable *hashtable, MVMuint32 mode) {
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

            struct MVMUniHashEntry *entry = (struct MVMUniHashEntry *) entry_raw;
            MVMuint32 ideal_bucket = entry->hash_val >> hashtable->key_right_shift;
            MVMint64 offset = 1 + bucket - ideal_bucket;
            int wrong_bucket = offset != *metadata;
            int wrong_order = offset < 1 || offset > prev_offset + 1;

            if (display == 2 || wrong_bucket || wrong_order) {
                fprintf(stderr, "%s%3X%c%3"PRIx64"%c%08X %s\n", prefix_hashes,
                        bucket, wrong_bucket ? '!' : ' ', offset,
                        wrong_order ? '!' : ' ', entry->hash_val, entry->key);
                errors += wrong_bucket + wrong_order;
            }
            prev_offset = offset;
        }
        ++bucket;
        ++metadata;
        entry_raw += sizeof(struct MVMUniHashEntry);
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