#include "moar.h"

#define STR_MIN_SIZE_BASE_2 3

/* Adapted from the log_base2 function.
 * https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
 * -- Individually, the code snippets here are in the public domain
 * -- (unless otherwise noted)
 * This one was not marked with any special copyright restriction.
 * What we need is to round the value rounded up to the next power of 2, and
 * then the log base 2 of that. Don't call this with v == 0. */
MVMuint32 MVM_round_up_log_base2(MVMuint32 v) {
    static const uint8_t MultiplyDeBruijnBitPosition[32] = {
        1, 10, 2, 11, 14, 22, 3, 30, 12, 15, 17, 19, 23, 26, 4, 31,
        9, 13, 21, 29, 16, 18, 25, 8, 20, 28, 24, 7, 27, 6, 5, 32
    };

    /* this rounds (v - 1) down to one less than a power of 2 */
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    return MultiplyDeBruijnBitPosition[(uint32_t)(v * 0x07C4ACDDU) >> 27];
}

MVM_STATIC_INLINE void hash_demolish_internal(MVMThreadContext *tc,
                                              struct MVMStrHashTableControl *control) {
    size_t actual_items = MVM_str_hash_kompromat(control);
    size_t entries_size = control->entry_size * actual_items;
    char *start = (char *)control - entries_size;
    MVM_free(start);
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_str_hash_demolish(MVMThreadContext *tc, MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (!control)
        return;
    hash_demolish_internal(tc, control);
    hashtable->table = NULL;
}
/* and then free memory if you allocated it */

MVM_STATIC_INLINE struct MVMStrHashTableControl *hash_allocate_common(MVMThreadContext *tc,
                                                                      MVMuint8 entry_size,
                                                                      MVMuint8 key_right_shift,
                                                                      MVMuint8 official_size_log2) {
    MVMuint32 official_size = 1 << (MVMuint32)official_size_log2;
    MVMuint32 max_items = official_size * MVM_STR_HASH_LOAD_FACTOR;
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
    size_t entries_size = entry_size * actual_items;
    size_t metadata_size = MVM_hash_round_size_up(actual_items + 1);
    size_t total_size
        = entries_size + sizeof(struct MVMStrHashTableControl) + metadata_size;

    struct MVMStrHashTableControl *control =
        (struct MVMStrHashTableControl *) ((char *)MVM_malloc(total_size) + entries_size);

    control->official_size_log2 = official_size_log2;
    control->max_items = max_items;
    control->cur_items = 0;
    control->probe_overflow_size = probe_overflow_size;
    control->key_right_shift = key_right_shift;
    control->entry_size = entry_size;

    MVMuint8 *metadata = (MVMuint8 *)(control + 1);
    memset(metadata, 0, metadata_size);

    /* A sentinel. This marks an occupied slot, at its ideal position. */
    metadata[actual_items] = 1;

#if MVM_HASH_RANDOMIZE
    control->salt = MVM_proc_rand_i(tc);
#else
    control->salt = 0;
#endif

    return control;
}

void MVM_str_hash_build(MVMThreadContext *tc,
                        MVMStrHashTable *hashtable,
                        MVMuint32 entry_size,
                        MVMuint32 entries)
{
    if (MVM_UNLIKELY(entry_size == 0 || entry_size > 255 || entry_size & 3)) {
        MVM_oops(tc, "Hash table entry_size %" PRIu32 " is invalid", entry_size);
    }

    MVMuint32 initial_size_base2;
    if (!entries) {
        initial_size_base2 = STR_MIN_SIZE_BASE_2;
    } else {
        /* Minimum size we need to allocate, given the load factor. */
        MVMuint32 min_needed = entries * (1.0 / MVM_STR_HASH_LOAD_FACTOR);
        initial_size_base2 = MVM_round_up_log_base2(min_needed);
        if (initial_size_base2 < STR_MIN_SIZE_BASE_2) {
            /* "Too small" - use our original defaults. */
            initial_size_base2 = STR_MIN_SIZE_BASE_2;
        }
    }

    struct MVMStrHashTableControl *control
        = hash_allocate_common(tc,
                               entry_size,
                               (8 * sizeof(MVMuint64) - initial_size_base2),
                               initial_size_base2);

#if HASH_DEBUG_ITER
#  if MVM_HASH_RANDOMIZE
    /* Given that we can embed the hashtable structure into other structures
     * (such as MVMHash) and those enclosing structures can be moved (GC!) we
     * can't use the address of this structure as its ID for debugging. We
     * could use the address of the first buckets array that we allocate, but if
     * we grow, then that memory could well be re-used for another hashtable,
     * and then we have two hashtables with the same ID, which rather defeats
     * the need to have (likely to be) unique IDs, to spot iterator leakage. */
    control->ht_id = control->salt;
#  else
    control->ht_id = MVM_proc_rand_i(tc);
#  endif
    control->serial = 0;
    control->last_delete_at = 0;
#endif

    hashtable->table = control;
}

MVM_STATIC_INLINE struct MVMStrHashHandle *hash_insert_internal(MVMThreadContext *tc,
                                                                struct MVMStrHashTableControl *control,
                                                                MVMString *key) {
    if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        MVM_oops(tc, "oops, attempt to recursively call grow when adding %p",
                 key);
    }

    struct MVM_hash_loop_state ls = MVM_str_hash_create_loop_state(tc, control, key);

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
                 * memmove(entry_raw + hashtable->entry_size, entry_raw,
                 *         hashtable->entry_size * entries_to_move);
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
#if HASH_DEBUG_ITER
            ++control->serial;
            control->last_delete_at = 0;
#endif

            *ls.metadata = ls.probe_distance;
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) ls.entry_raw;
            entry->key = NULL;
            return entry;
        }

        if (*ls.metadata == ls.probe_distance) {
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) ls.entry_raw;
            if (entry->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, entry->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           entry->key, 0))) {
                return entry;
            }
        }
        ++ls.probe_distance;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + MVM_str_hash_max_items(control));
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + 256);
    }
}

void *MVM_str_hash_lvalue_fetch_nocheck(MVMThreadContext *tc,
                                        MVMStrHashTable *hashtable,
                                        MVMString *key) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(!control)) {
        MVM_oops(tc, "Attempting insert on MVM_str_hash without first calling MVM_str_hash_build");
    }
    else if (MVM_UNLIKELY(control->cur_items >= control->max_items)) {
        /* We should avoid growing the hash if we don't need to.
         * It's expensive, and for hashes with iterators, growing the hash
         * invalidates iterators. Which is buggy behaviour if the fetch doesn't
         * need to create a key. */
        struct MVMStrHashHandle *entry = MVM_str_hash_fetch_nocheck(tc, hashtable, key);
        if (entry) {
            return entry;
        }

        MVMuint32 true_size =  MVM_str_hash_kompromat(control);
        MVMuint8 *entry_raw_orig = MVM_str_hash_entries(control);
        MVMuint8 *metadata_orig = MVM_str_hash_metadata(control);

        struct MVMStrHashTableControl *control_orig = control;

        control = hash_allocate_common(tc,
                                       control_orig->entry_size,
                                       control_orig->key_right_shift - 1,
                                       control_orig->official_size_log2 + 1);

#if HASH_DEBUG_ITER
        control->ht_id = control_orig->ht_id;
        control->serial = control_orig->serial;
        control->last_delete_at = control_orig->last_delete_at;
#endif
        hashtable->table = control;

        MVMuint8 *entry_raw = entry_raw_orig;
        MVMuint8 *metadata = metadata_orig;
        MVMHashNumItems bucket = 0;
        while (bucket < true_size) {
            if (*metadata) {
                struct MVMStrHashHandle *old_entry = (struct MVMStrHashHandle *) entry_raw;
                void *new_entry_raw = hash_insert_internal(tc, control, old_entry->key);
                struct MVMStrHashHandle *new_entry = (struct MVMStrHashHandle *) new_entry_raw;
                assert(new_entry->key == NULL);
                memcpy(new_entry, old_entry, control->entry_size);
            }
            ++bucket;
            ++metadata;
            entry_raw -= control->entry_size;
        }
        hash_demolish_internal(tc, control_orig);
    }
    return hash_insert_internal(tc, control, key);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care.
 * (well that's the official line. As you can see, the exception suggests we
 * currently don't exploit the documented freedom, and actually sanity check
 * what we are given.) */
void *MVM_str_hash_insert_nocheck(MVMThreadContext *tc,
                                  MVMStrHashTable *hashtable,
                                  MVMString *key) {
    struct MVMStrHashHandle *new_entry = MVM_str_hash_lvalue_fetch(tc, hashtable, key);
    if (new_entry->key) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, key);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "insert duplicate key %s",
                                       c_name);
    } else {
        new_entry->key = key;
    }
    return new_entry;
}


void MVM_str_hash_delete_nocheck(MVMThreadContext *tc,
                                 MVMStrHashTable *hashtable,
                                 MVMString *key) {
    if (MVM_str_hash_is_empty(tc, hashtable)) {
        return;
    }

    struct MVMStrHashTableControl *control = hashtable->table;
    struct MVM_hash_loop_state ls = MVM_str_hash_create_loop_state(tc, control, key);

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) ls.entry_raw;
            if (entry->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, entry->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           entry->key, 0))) {
                /* Target acquired. */

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
                     * memmove(entry_raw, entry_raw + hashtable->entry_size,
                     *         hashtable->entry_size * entries_to_move);
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

#if HASH_DEBUG_ITER
                ++control->serial;
                /* We need to calculate the interator position corresponding to the
                 * hash bucket we actually used.
                 * `metadata - ...metadata(...)` gives the bucket, and
                 * iterators store `$bucket + 1`, hence: */
                control->last_delete_at = 1 + ls.metadata - MVM_str_hash_metadata(control);
#endif

                /* Job's a good 'un. */
                return;
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
            return;
        }
        ++ls.probe_distance;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + MVM_str_hash_max_items(control));
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + 256);
    }
}


/* This is not part of the public API, and subject to change at any point.
   (possibly in ways that are actually incompatible but won't generate compiler
   warnings.) */
MVMuint64 MVM_str_hash_fsck(MVMThreadContext *tc, MVMStrHashTable *hashtable, MVMuint32 mode) {
    struct MVMStrHashTableControl *control = hashtable->table;
    const char *prefix_hashes = mode & MVM_HASH_FSCK_PREFIX_HASHES ? "# " : "";
    MVMuint32 display = mode & 3;
    MVMuint64 errors = 0;
    MVMuint64 seen = 0;

    if (MVM_str_hash_entries(control) == NULL) {
        if (display) {
            fprintf(stderr, "%s NULL %p (empty)\n", prefix_hashes, hashtable);
        }
        return 0;
    }

    MVMuint32 true_size = MVM_str_hash_kompromat(control);
    MVMuint8 *entry_raw = MVM_str_hash_entries(control);
    MVMuint8 *metadata = MVM_str_hash_metadata(control);
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

            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) entry_raw;
            char *problem = NULL;
            MVMString *key = NULL;
            if (!entry) {
                problem = "entry NULL";
            } else {
                key = entry->key;
                if (!key) {
                    problem = "key NULL";
                }
            }
            if (!problem) {
                if ((MVMObject *)key == tc->instance->VMNull) {
                    problem = "VMNull";
                } else {
                    if (mode & MVM_HASH_FSCK_CHECK_FROMSPACE) {
                        MVMThread *cur_thread = tc->instance->threads;
                        while (cur_thread) {
                            MVMThreadContext *thread_tc = cur_thread->body.tc;
                            if (thread_tc && thread_tc->nursery_fromspace &&
                            (char *)(key) >= (char *)thread_tc->nursery_fromspace &&
                            (char *)(key) < (char *)thread_tc->nursery_fromspace +
                                thread_tc->nursery_fromspace_size) {
                                problem = "fromspace";
                                break;
                            }
                            cur_thread = cur_thread->body.next;
                        }
                    }
                }
            }
            if (!problem) {
                if (((MVMCollectable *)key)->flags1 & MVM_CF_DEBUG_IN_GEN2_FREE_LIST) {
                    problem = "gen2 freelist";
                } else if (REPR(key)->ID != MVM_REPR_ID_MVMString) {
                    problem = "not a string";
                } else if (!IS_CONCRETE(key)) {
                    problem = "type object";
                }
            }

            if (problem) {
                ++errors;
                if (display) {
                    fprintf(stderr, "%s%3X! %s\n", prefix_hashes, bucket, problem);
                }
                /* We don't have any great choices here. The metadata signals a
                 * key, but as it is corrupt, we can't compute its ideal bucket,
                 * and hence its offset, so we can't remember that for the next
                 * bucket. */
                prev_offset = 0;
            } else {
                /* OK, it is a concrete string (still). */
                MVMuint64 hash_val = MVM_str_hash_code(tc, control->salt, key);
                MVMuint32 ideal_bucket = hash_val >> control->key_right_shift;
                MVMint64 offset = 1 + bucket - ideal_bucket;
                int wrong_bucket = offset != *metadata;
                int wrong_order = offset < 1 || offset > prev_offset + 1;

                if (display == 2
                    || (display == 1 && (wrong_bucket || wrong_order))) {
                    /* And if you think that these two are overkill, you haven't
                     * had to debug this stuff :-/ */
                    char open;
                    char close;
                    if (((MVMCollectable *)key)->flags1 & MVM_CF_SECOND_GEN) {
                        open = '{';
                        close = '}';
                    } else if (((MVMCollectable *)key)->flags1 & MVM_CF_NURSERY_SEEN) {
                        open = '[';
                        close = ']';
                    } else {
                        open = '(';
                        close = ')';
                    }

                    MVMuint64 len = MVM_string_graphs(tc, key);
                    if (mode & MVM_HASH_FSCK_KEY_VIA_API) {
                        char *c_key = MVM_string_utf8_encode_C_string(tc, key);
                        fprintf(stderr,
                                "%s%3X%c%3"PRIx64"%c%0"PRIx64" %c%2"PRIu64"%c %p %s\n",
                                prefix_hashes,
                                bucket, wrong_bucket ? '!' : ' ',
                                offset, wrong_order ? '!' : ' ', hash_val,
                                open, len, close, key, c_key);
                        MVM_free(c_key);
                    } else {
                        /* Cheat. Don't use the API.
                         * (Doesn't allocate, so can't deadlock.) */
                        if (key->body.storage_type == MVM_STRING_GRAPHEME_ASCII && len < 0xFFF) {
                            fprintf(stderr,
                                    "%s%3X%c%3"PRIx64"%c%0"PRIx64" %c%2"PRIu64"%c %p \"%*s\"\n",
                                    prefix_hashes, bucket,
                                    wrong_bucket ? '!' : ' ', offset,
                                    wrong_order ? '!' : ' ', hash_val,
                                    open, len, close, key,
                                    (int) len, key->body.storage.blob_ascii);
                        } else {
                            fprintf(stderr,
                                    "%s%3X%c%3"PRIx64"%c%0"PRIx64" %c%2"PRIu64"%c %p %u@%p\n",
                                    prefix_hashes,
                                    bucket, wrong_bucket ? '!' : ' ',
                                    offset, wrong_order ? '!' : ' ', hash_val,
                                    open, len, close, key,
                                    key->body.storage_type, key);
                        }
                    }
                }
                errors += wrong_bucket + wrong_order;
                prev_offset = offset;
            }
        }
        ++bucket;
        ++metadata;
        entry_raw -= control->entry_size;
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
            fprintf(stderr, "%s %"PRIx64" != %"PRIx32"\n",
                    prefix_hashes, seen, control->cur_items);
        }
    }

    return errors;
}
