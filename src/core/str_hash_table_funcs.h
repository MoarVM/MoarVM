/* These are private. We need them out here for the inline functions. Use those.
 */
/* See comments in hash_allocate_common (and elsewhere) before changing the
 * load factor, or STR_MIN_SIZE_BASE_2 or MVM_HASH_INITIAL_BITS_IN_METADATA,
 * and test with assertions enabled. The current choices permit certain
 * optimisation assumptions in parts of the code. */
#define MVM_STR_HASH_LOAD_FACTOR 0.75
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_official_size(const struct MVMStrHashTableControl *control) {
    assert(!(control->cur_items == 0 && control->max_items == 0));
    return 1 << (MVMuint32)control->official_size_log2;
}
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_max_items(const struct MVMStrHashTableControl *control) {
    assert(!(control->cur_items == 0 && control->max_items == 0));
    return MVM_str_hash_official_size(control) * MVM_STR_HASH_LOAD_FACTOR;
}
/* -1 because...
 * probe distance of 1 is the correct bucket.
 * hence for a value whose ideal slot is the last bucket, it's *in* the official
 * allocation.
 * probe distance of 2 is the first extra bucket beyond the official allocation
 * probe distance of 255 is the 254th beyond the official allocation.
 */
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_allocated_items(const struct MVMStrHashTableControl *control) {
    assert(!(control->cur_items == 0 && control->max_items == 0));
    return MVM_str_hash_official_size(control) + control->max_probe_distance_limit - 1;
}
/* Returns the number of buckets that the hash is using. This can be lower than
 * the number of buckets allocated, as the last bucket that can be used is at
 * max_probe_distance - 1, whereas buckets are allocated up to
 * max_probe_distance_limit - 1. And if the hash is empty, it can't be using
 * any buckets. As the name is meant to imply, this function is private. */
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_kompromat(const struct MVMStrHashTableControl *control) {
    if (MVM_UNLIKELY(control->cur_items == 0))
        return 0;
    return MVM_str_hash_official_size(control) + control->max_probe_distance - 1;
}
MVM_STATIC_INLINE MVMuint8 *MVM_str_hash_metadata(const struct MVMStrHashTableControl *control) {
    assert(!(control->cur_items == 0 && control->max_items == 0));
    return (MVMuint8 *) control + sizeof(struct MVMStrHashTableControl);
}
MVM_STATIC_INLINE MVMuint8 *MVM_str_hash_entries(const struct MVMStrHashTableControl *control) {
    assert(!(control->cur_items == 0 && control->max_items == 0));
    return (MVMuint8 *) control - control->entry_size;
}

/* round up to a multiple of sizeof(long). My assumption is that this is won't
 * cause any extra allocation, but will both be faster for memcpy/memset, and
 * also a natural size for processing the metadata array in chunks larger than
 * byte-by-byte. */
MVM_STATIC_INLINE size_t MVM_hash_round_size_up(size_t wanted) {
    return (wanted - 1 + sizeof(long)) & ~(sizeof(long) - 1);
}

MVM_STATIC_INLINE size_t MVM_str_hash_allocated_size(MVMThreadContext *tc, MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (!control)
        return 0;
    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_allocated_size called with a stale hashtable pointer");
    }

    if (control->cur_items == 0 && control->max_items == 0)
        return sizeof(*control);

    size_t allocated_items = MVM_str_hash_allocated_items(control);
    size_t entries_size = control->entry_size * allocated_items;
    size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
    return entries_size + sizeof(struct MVMStrHashTableControl) + metadata_size;
}

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_str_hash_demolish(MVMThreadContext *tc, MVMStrHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it. */
void MVM_str_hash_build(MVMThreadContext *tc,
                        MVMStrHashTable *hashtable,
                        MVMuint32 entry_size,
                        MVMuint32 entries);

MVM_STATIC_INLINE int MVM_str_hash_is_empty(MVMThreadContext *tc,
                                            MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops(tc, "MVM_str_hash_is_empty called with a stale hashtable pointer");
    }
    return !control || control->cur_items == 0;
}

/* This code assumes that the destination hash is uninitialised - ie not even
 * MVM_str_hash_build has been called upon it. */
MVM_STATIC_INLINE void MVM_str_hash_shallow_copy(MVMThreadContext *tc,
                                                 MVMStrHashTable *source,
                                                 MVMStrHashTable *dest) {
    const struct MVMStrHashTableControl *control = source->table;
    if (!control)
        return;
    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_shallow_copy called with a stale hashtable pointer");
    }
    if (control->cur_items == 0 && control->max_items == 0) {
        struct MVMStrHashTableControl *empty = MVM_malloc(sizeof(*empty));
        memcpy(empty, control, sizeof(*empty));
        dest->table = empty;
    } else {
        size_t allocated_items = MVM_str_hash_allocated_items(control);
        size_t entries_size = control->entry_size * allocated_items;
        size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
        const char *start = (const char *)control - entries_size;
        size_t total_size
            = entries_size + sizeof(struct MVMStrHashTableControl) + metadata_size;
        char *target = (char *) MVM_malloc(total_size);
        memcpy(target, start, total_size);
        dest->table = (struct MVMStrHashTableControl *)(target + entries_size);
    }
#if HASH_DEBUG_ITER
    dest->table->ht_id = MVM_proc_rand_i(tc);
#endif
    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_shallow_copy called with hashtable pointer that turned stale");
    }
}

MVM_STATIC_INLINE MVMuint64 MVM_str_hash_code(MVMThreadContext *tc,
                                              MVMuint64 salt,
                                              MVMString *key) {
    return (MVM_string_hash_code(tc, key) ^ salt) * UINT64_C(11400714819323198485);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void *MVM_str_hash_insert_nocheck(MVMThreadContext *tc,
                                  MVMStrHashTable *hashtable,
                                  MVMString *key);

struct MVM_hash_loop_state {
    MVMuint8 *entry_raw;
    MVMuint8 *metadata;
    unsigned int metadata_increment;
    unsigned int metadata_hash_mask;
    unsigned int probe_distance_shift;
    unsigned int max_probe_distance;
    unsigned int probe_distance;
    MVMuint16 entry_size;
};

/* This function turns out to be quite hot. We initialise looping on a *lot* of
 * hashes. Removing a branch from this function reduced the instruction dispatch
 * by over 0.1% for a non-trivial program (according to cachegrind. Sure, it
 * doesn't change the likely cache misses, which is the first-order driver of
 * performance.)
 *
 * Inspecting annotated compiler assembly output suggests that optimisers move
 * the sections of this function around in the inlined code, and hopefully don't
 * initialised any values until they are used. */

MVM_STATIC_INLINE struct MVM_hash_loop_state
MVM_str_hash_create_loop_state(MVMThreadContext *tc,
                               struct MVMStrHashTableControl *control,
                               MVMString *key) {
    MVMuint64 hash_val = MVM_str_hash_code(tc, control->salt, key);
    struct MVM_hash_loop_state retval;
    retval.entry_size = control->entry_size;
    retval.metadata_increment = 1 << control->metadata_hash_bits;
    retval.metadata_hash_mask = retval.metadata_increment - 1;
    retval.probe_distance_shift = control->metadata_hash_bits;
    retval.max_probe_distance = control->max_probe_distance;

    unsigned int used_hash_bits = hash_val >> control->key_right_shift;
    retval.probe_distance = retval.metadata_increment | (used_hash_bits & retval.metadata_hash_mask);
    MVMHashNumItems bucket = used_hash_bits >> control->metadata_hash_bits;
    if (!control->metadata_hash_bits) {
        assert(retval.probe_distance == 1);
        assert(retval.metadata_hash_mask == 0);
        assert(bucket == used_hash_bits);
    }

    retval.entry_raw = MVM_str_hash_entries(control) - bucket * retval.entry_size;
    retval.metadata = MVM_str_hash_metadata(control) + bucket;
    return retval;
}

MVM_STATIC_INLINE void *MVM_str_hash_fetch_nocheck(MVMThreadContext *tc,
                                                   MVMStrHashTable *hashtable,
                                                   MVMString *key) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops(tc, "MVM_str_hash_fetch_nocheck called with a stale hashtable pointer");
    }

    if (MVM_str_hash_is_empty(tc, hashtable)) {
        return NULL;
    }

    struct MVM_hash_loop_state ls = MVM_str_hash_create_loop_state(tc, control, key);

    /* Comments in str_hash_table.h describe the various invariants.
     * Another way of thinking about them, which is hard to draw in ASCII (or
     * Unicode) because we don't have a way to draw diagonal lines of different
     * gradients. So look at the second diagram in
     * https://martin.ankerl.com/2016/09/21/very-fast-hashmap-in-c-part-2/
     *
     * The Robin Hood invariant meant that all entries contesting the same
     * "ideal" bucket are in one contiguous block in memory. Each of these
     * (variable sized) groups is strictly in the same order as the
     * (fixed size of 1) ideal buckets.
     * Assuming higher memory drawn to the right, drawing lines from the "ideal"
     * bucket to the 1+ entries contesting it, no lines will cross.
     * (The gradient of the line is the probe distance).
     *
     * Robin Hood makes no constraint on the order of entries within each group.
     * The "hash bits in metadata" implementation *does* - entries within each
     * group must be sorted by (used) extra hash metadata bits, ascending.
     *
     * (Order for 2+ entries with identical extra hash value bits does not
     * matter. For such entries, this means that the hash values are identical
     * in the topmost $n + $m bits, where we use $n to determine the "ideal"
     * bucket, and $m more in the metadata. It's only these "unordered" entries
     * that we even consider for the full key-is-equal test)
     *
     * Implicit in all of this (I think) is that effectively the loop structure
     * below is not the *only* way of looking at the search for an entry.
     * Instead of this one loop with a test, one could have 2 loops
     *
     * while (*ls.metadata > ls.probe_distance) {
     *     ls.probe_distance += ls.metadata_increment;
     *      ++ls.metadata;
     * }
     * initialise entry raw...
     * while (*ls.metadata == ls.probe_distance) {
     *     key checking...
     *         return if found...
     *     ls.probe_distance += ls.metadata_increment;
     *     ++ls.metadata;
     *     ls.entry_raw -= ls.entry_size;
     * }
     * return not found
     *
     * This might be marginally more efficient, but benchmarking so far suggests
     * that it's not obvious whether any win is worth the complexity trade off.
     *
     * As to whether we should (also) add a check on the (64 bit) values of
     * MVM_string_hash_code() for key and entry->key (after the pointer
     * comparison)
     *
     * I'm sure that someone documented this online much better than I can
     * explain it, but I can't find it to link to it.
     *
     * "It might not be worth it" (so if you want to try, benchmark it)
     *
     * Specifically, the way this loop works (see just above), if we're using
     * $n bits from the hash to chose the "ideal" bucket, and because the probe
     * distance test (of regular Robin Hood hashing) already avoids the need to
     * even compare keys for entries not from "our" "ideal" bucket, we already
     * know that at best only 64 - $n bits of hash value might differ.
     * As we're *also* using $m bits of hash in the metadata (for $m < $n),
     * the metadata loop means that we only get to the point of comparing keys
     * iff $n - $m bits of hash are the same, Meaning that at most we only have
     * a 1 in (64 - $n - $m) chance of the comparison (correctly) rejecting a
     * key without us needing to hit the full string comparison.
     * So it's not as big a win as it first might seem to be, particularly for
     * larger hashes where $n is larger.
     *
     * For a *hit* (a found key) we have to do the string comparison anyway.
     * So the only gain of adding the hash test first (two 64 bit loads and a
     * comparison, and it's only that frugal if one breaks some encapsulation)
     * is if we perform lots of lookups that are misses
     * (not found in the hash, or map to the same "ideal" bucket)
     *
     * And given that the *next* check is `MVM_string_graphs_nocheck` we would
     * only win if we have "lots" of strings that map to close-enough hash
     * values and happen to be the same length. Is this that likely?
     */

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) ls.entry_raw;
            if (entry->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, entry->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           entry->key, 0))) {
                if (MVM_UNLIKELY(control->stale)) {
                    MVM_oops(tc, "MVM_str_hash_fetch_nocheck called with a hashtable pointer that turned stale");
                }
                return entry;
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
            if (MVM_UNLIKELY(control->stale)) {
                MVM_oops(tc, "MVM_str_hash_fetch_nocheck called with a hashtable pointer that turned stale");
            }

            return NULL;
        }
        /* This is actually the "body" of the lookup loop. gcc on 32 bit arm
         * gets the entire loop (arithmetic, 1 test, two branches) down to 8
         * instructions. By replacing `ls.entry_raw` and its update in the loop
         * with a calcuation above we can get the loop down to 7 instructions,
         * but cachegrind (on x86_64) thinks that it results in slightly more
         * instructions dispatched, more memory accesses and more cache misses.
         * The extra complexity probably isn't worth it. Look elsewhere for
         * savings. */
        ls.probe_distance += ls.metadata_increment;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance < (ls.max_probe_distance + 2) * ls.metadata_increment);
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + MVM_str_hash_max_items(control));
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + 256);
    }
}

/* 1:1 copy of the above, but call MVM_oops_with_blame instead of MVM_oops */
MVM_STATIC_INLINE void *MVM_str_hash_fetch_nocheck_blame(MVMThreadContext *tc,
                                                   MVMStrHashTable *hashtable,
                                                   MVMString *key, MVMObject *blame) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops_with_blame(tc, blame, "MVM_str_hash_fetch_nocheck called with a stale hashtable pointer");
    }

    if (MVM_str_hash_is_empty(tc, hashtable)) {
        return NULL;
    }

    struct MVM_hash_loop_state ls = MVM_str_hash_create_loop_state(tc, control, key);

    while (1) {
        if (*ls.metadata == ls.probe_distance) {
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) ls.entry_raw;
            if (entry->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, entry->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           entry->key, 0))) {
                if (MVM_UNLIKELY(control->stale)) {
                    MVM_oops_with_blame(tc, blame, "MVM_str_hash_fetch_nocheck called with a hashtable pointer that turned stale");
                }
                return entry;
            }
        }
        else if (*ls.metadata < ls.probe_distance) {
            if (MVM_UNLIKELY(control->stale)) {
                MVM_oops_with_blame(tc, blame, "MVM_str_hash_fetch_nocheck called with a hashtable pointer that turned stale");
            }

            return NULL;
        }
        ls.probe_distance += ls.metadata_increment;
        ++ls.metadata;
        ls.entry_raw -= ls.entry_size;
        assert(ls.probe_distance < (ls.max_probe_distance + 2) * ls.metadata_increment);
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + MVM_str_hash_max_items(control));
        assert(ls.metadata < MVM_str_hash_metadata(control) + MVM_str_hash_official_size(control) + 256);
    }
}


/* Looks up entry for key, creating it if necessary.
 * Returns the structure we indirect to.
 * If it's freshly allocated, then *entry is NULL (you need to fill this in)
 * and everything else is uninitialised.
 * This might seem like a quirky API, but it's intended to fill a common pattern
 * we have, and the use of NULL key avoids needing two return values.
 * DON'T FORGET to fill in the NULL key. */
void *MVM_str_hash_lvalue_fetch_nocheck(MVMThreadContext *tc,
                                        MVMStrHashTable *hashtable,
                                        MVMString *key);

void *MVM_str_hash_lvalue_fetch_nocheck_blame(MVMThreadContext *tc,
                                        MVMStrHashTable *hashtable,
                                        MVMString *key, MVMObject *blame);


void MVM_str_hash_delete_nocheck(MVMThreadContext *tc,
                                 MVMStrHashTable *hashtable,
                                 MVMString *want);

/* Use these two functions to
 * 1: move the is-concrete-or-throw test before critical sections (eg before
 *    locking a mutex) or before allocation
 * 2: put cleanup code between them that executes before the exception is thrown
 */

MVM_STATIC_INLINE int MVM_str_hash_key_is_valid(MVMThreadContext *tc,
                                                MVMString *key) {
    return MVM_LIKELY(!MVM_is_null(tc, (MVMObject *)key)
                      && REPR(key)->ID == MVM_REPR_ID_MVMString
                      && IS_CONCRETE(key)) ? 1 : 0;
}

MVM_STATIC_INLINE void MVM_str_hash_key_throw_invalid(MVMThreadContext *tc,
                                                      MVMString *key) {
    MVM_exception_throw_adhoc(tc, "Hash keys must be concrete strings (got %s)",
                              MVM_6model_get_debug_name(tc, (MVMObject *)key));
}

/* Looks up entry for key, creating it if necessary.
 * Returns an entry.
 * If it's freshly allocated, then entry->key is NULL (you need to fill this in)
 * and everything else is uninitialised.
 * This might seem like a quirky API, but it's intended to fill a common pattern
 * we have, and the use of NULL key avoids needing two return values.
 * DON'T FORGET to fill in the NULL key. */
MVM_STATIC_INLINE void *MVM_str_hash_lvalue_fetch(MVMThreadContext *tc,
                                                  MVMStrHashTable *hashtable,
                                                  MVMString *key) {
    if (!MVM_str_hash_key_is_valid(tc, key)) {
        MVM_str_hash_key_throw_invalid(tc, key);
    }
    return MVM_str_hash_lvalue_fetch_nocheck(tc, hashtable, key);
}

MVM_STATIC_INLINE void *MVM_str_hash_lvalue_fetch_blame(MVMThreadContext *tc,
                                                  MVMStrHashTable *hashtable,
                                                  MVMString *key, MVMObject *blame) {
    if (!MVM_str_hash_key_is_valid(tc, key)) {
        MVM_str_hash_key_throw_invalid(tc, key);
    }
    return MVM_str_hash_lvalue_fetch_nocheck_blame(tc, hashtable, key, blame);
}

MVM_STATIC_INLINE void *MVM_str_hash_fetch(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    return MVM_str_hash_fetch_nocheck(tc, hashtable, want);
}

MVM_STATIC_INLINE void *MVM_str_hash_fetch_blame(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want, MVMObject *blame) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    return MVM_str_hash_fetch_nocheck_blame(tc, hashtable, want, blame);
}

MVM_STATIC_INLINE void MVM_str_hash_delete(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    MVM_str_hash_delete_nocheck(tc, hashtable, want);
}

/* This is not part of the public API, and subject to change at any point.
 * (possibly in ways that are actually incompatible but won't generate compiler
 * warnings.)
 * Currently 0 is a check with no display, and it always returns the error
 * count. */
enum {
    MVM_HASH_FSCK_DISPLAY_NONE      = 0x00,
    MVM_HASH_FSCK_DISPLAY_ERRORS    = 0x01,
    MVM_HASH_FSCK_DISPLAY_ALL       = 0x02,
    MVM_HASH_FSCK_PREFIX_HASHES     = 0x04,
    MVM_HASH_FSCK_KEY_VIA_API       = 0x08, /* not just ASCII keys, might deadlock */
    MVM_HASH_FSCK_CHECK_FROMSPACE   = 0x10  /* O(n) test. */
};

MVMuint64 MVM_str_hash_fsck(MVMThreadContext *tc, MVMStrHashTable *hashtable, MVMuint32 mode);

/* iterators are stored as unsigned values, metadata index plus one.
 * This is clearly an internal implementation detail. Don't cheat.
 */

/* Only call this if MVM_str_hash_at_end returns false. */
MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_next_nocheck(MVMThreadContext *tc,
                                                               MVMStrHashTable *hashtable,
                                                               MVMStrHashIterator iterator) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_iterator_next_nocheck called with a stale hashtable pointer");
    }

    /* Whilst this looks like it can be optimised to word at a time skip ahead.
     * (Beware of endianness) it isn't easy *yet*, because one can overrun the
     * allocated buffer, and that makes ASAN very excited. */
    while (--iterator.pos > 0) {
        if (MVM_str_hash_metadata(control)[iterator.pos - 1]) {
            return iterator;
        }
    }
    return iterator;
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_next(MVMThreadContext *tc,
                                                       MVMStrHashTable *hashtable,
                                                       MVMStrHashIterator iterator) {
#if HASH_DEBUG_ITER
    struct MVMStrHashTableControl *control = hashtable->table;
    if (iterator.owner != control->ht_id) {
        MVM_oops(tc, "MVM_str_hash_next called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, control->ht_id);
    }
    /* "the usual case" is that the iterator serial number  matches the hash
     * serial number.
     * As we permit deletes at the current iterator, we also track whether the
     * last mutation on the hash was a delete, and if so record where. Hence,
     * if the hash serial has advanced by one, and the last delete was at this
     * iterator's current bucket position, that's OK too. */
    if (!(iterator.serial == control->serial
          || (iterator.serial == control->serial - 1 &&
              iterator.pos == control->last_delete_at))) {
        MVM_oops(tc, "MVM_str_hash_next called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, control->serial);
    }
#endif

    if (iterator.pos == 0) {
        MVM_oops(tc, "Calling str_hash_next when iterator is already at the end");
    }

    return MVM_str_hash_next_nocheck(tc, hashtable, iterator);
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_first(MVMThreadContext *tc,
                                                        MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    MVMStrHashIterator iterator;

    if (!control) {
        /* This hash has not even been built yet. We return an iterator that is
         * already "at the end" */
#if HASH_DEBUG_ITER
        iterator.owner = iterator.serial = 0;
#endif
        iterator.pos = 0;
        return iterator;
    }

    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_iterator_first called with a stale hashtable pointer");
    }

#if HASH_DEBUG_ITER
    iterator.owner = control->ht_id;
    iterator.serial = control->serial;
#endif

    if (control->cur_items == 0) {
        /* The hash is empty. No need to do the work to find the "first" item
         * when we know that there are none. Return an iterator at the end. */
        iterator.pos = 0;
        return iterator;
    }

    iterator.pos = MVM_str_hash_kompromat(control);

    if (MVM_str_hash_metadata(control)[iterator.pos - 1]) {
        return iterator;
    }
    return MVM_str_hash_next(tc, hashtable, iterator);
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_start(MVMThreadContext *tc,
                                                        MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    MVMStrHashIterator retval;
    if (MVM_UNLIKELY(!control)) {
#if HASH_DEBUG_ITER
        retval.owner = retval.serial = 0;
#endif
        retval.pos = 1;
        return retval;
    }

    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_start called with a stale hashtable pointer");
    }

#if HASH_DEBUG_ITER
    retval.owner = control->ht_id;
    retval.serial = control->serial;
#endif
    retval.pos = MVM_str_hash_kompromat(control) + 1;
    return retval;
}

MVM_STATIC_INLINE int MVM_str_hash_at_start(MVMThreadContext *tc,
                                            MVMStrHashTable *hashtable,
                                            MVMStrHashIterator iterator) {
    struct MVMStrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(!control)) {
        return iterator.pos == 1;
    }

    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_at_start called with a stale hashtable pointer");
    }

#if HASH_DEBUG_ITER
    if (iterator.owner != control->ht_id) {
        MVM_oops(tc, "MVM_str_hash_at_start called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, control->ht_id);
    }
    if (iterator.serial != control->serial) {
        MVM_oops(tc, "MVM_str_hash_at_start called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, control->serial);
    }
#endif
    return iterator.pos == MVM_str_hash_kompromat(control) + 1;
}

/* Only call this if MVM_str_hash_at_end returns false. */
MVM_STATIC_INLINE void *MVM_str_hash_current_nocheck(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable,
                                                     MVMStrHashIterator iterator) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control->stale)) {
        MVM_oops(tc, "MVM_str_hash_current_nocheck called with a stale hashtable pointer");
    }

    assert(MVM_str_hash_metadata(control)[iterator.pos - 1]);
    return MVM_str_hash_entries(control) - control->entry_size * (iterator.pos - 1);
}

/* FIXME - this needs a better name: */
MVM_STATIC_INLINE void *MVM_str_hash_current(MVMThreadContext *tc,
                                             MVMStrHashTable *hashtable,
                                             MVMStrHashIterator iterator) {
#if HASH_DEBUG_ITER
    const struct MVMStrHashTableControl *control = hashtable->table;
    if (iterator.owner != control->ht_id) {
        MVM_oops(tc, "MVM_str_hash_current called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, control->ht_id);
    }
    if (iterator.serial != control->serial) {
        MVM_oops(tc, "MVM_str_hash_current called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, control->serial);
    }
#endif

    /* This is MVM_str_hash_at_end without the HASH_DEBUG_ITER checks duplicated. */
    if (MVM_UNLIKELY(iterator.pos == 0)) {
        /* Bother. This seems to be part of our de-facto API. */
        return NULL;
    }

    return MVM_str_hash_current_nocheck(tc, hashtable, iterator);
}

MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_count(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops(tc, "MVM_str_hash_count called with a stale hashtable pointer");
    }

    return control ? control->cur_items : 0;
}

MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_count_blame(MVMThreadContext *tc,
                                                           MVMStrHashTable *hashtable, MVMObject *blame) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops_with_blame(tc, blame, "MVM_str_hash_count called with a stale hashtable pointer");
    }

    return control ? control->cur_items : 0;
}


/* If this returns 0, then you have not yet called MVM_str_hash_build */
MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_entry_size(MVMThreadContext *tc,
                                                          MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops(tc, "MVM_str_hash_entry_size called with a stale hashtable pointer");
    }

    return control ? control->entry_size : 0;
}

MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_entry_size_blame(MVMThreadContext *tc,
                                                                MVMStrHashTable *hashtable, MVMObject *blame) {
    struct MVMStrHashTableControl *control = hashtable->table;

    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops_with_blame(tc, blame, "MVM_str_hash_entry_size called with a stale hashtable pointer");
    }

    return control ? control->entry_size : 0;
}
