/* These are private. We need them out here for the inline functions. Use those.
 */
/* See comments in hash_allocate_common (and elsewhere) before changing the
 * load factor, or STR_MIN_SIZE_BASE_2 or MVM_HASH_INITIAL_BITS_IN_METADATA,
 * and test with assertions enabled. The current choices permit certain
 * optimisation assumptions in parts of the code. */
#define MVM_STR_HASH_LOAD_FACTOR 0.75
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_official_size(const struct MVMStrHashTableControl *control) {
    return 1 << (MVMuint32)control->official_size_log2;
}
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_max_items(const struct MVMStrHashTableControl *control) {
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
    return MVM_str_hash_official_size(control) + control->max_probe_distance_limit - 1;
}
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_kompromat(const struct MVMStrHashTableControl *control) {
    return MVM_str_hash_official_size(control) + control->max_probe_distance - 1;
}
MVM_STATIC_INLINE MVMuint8 *MVM_str_hash_metadata(const struct MVMStrHashTableControl *control) {
    return (MVMuint8 *) control + sizeof(struct MVMStrHashTableControl);
}
MVM_STATIC_INLINE MVMuint8 *MVM_str_hash_entries(const struct MVMStrHashTableControl *control) {
    return (MVMuint8 *) control - control->entry_size;
}

/* round up to a multiple of sizeof(long). My assumption is that this is won't
 * cause any extra allocation, but will both be faster for memcpy/memset, and
 * also a natural size for processing the metadata array in chunks larger than
 * byte-by-byte. */
MVM_STATIC_INLINE size_t MVM_hash_round_size_up(size_t wanted) {
    return (wanted - 1 + sizeof(long)) & ~(sizeof(long) - 1);
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
    size_t allocated_items = MVM_str_hash_allocated_items(control);
    size_t entries_size = control->entry_size * allocated_items;
    size_t metadata_size = MVM_hash_round_size_up(allocated_items + 1);
    const char *start = (const char *)control - entries_size;
    size_t total_size
        = entries_size + sizeof(struct MVMStrHashTableControl) + metadata_size;
    char *target = MVM_malloc(total_size);
    memcpy(target, start, total_size);
    dest->table = (struct MVMStrHashTableControl *)(target + entries_size);
#if HASH_DEBUG_ITER
    dest->table->ht_id = MVM_proc_rand_i(tc);
#endif
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

MVM_STATIC_INLINE struct MVM_hash_loop_state
MVM_str_hash_create_loop_state(MVMThreadContext *tc,
                               struct MVMStrHashTableControl *control,
                               MVMString *key) {
    MVMuint64 hash_val = MVM_str_hash_code(tc, control->salt, key);
    MVMHashNumItems bucket = hash_val >> control->key_right_shift;
    struct MVM_hash_loop_state retval;
    retval.entry_size = control->entry_size;
    retval.metadata_increment = 1 << control->metadata_hash_bits;
    retval.metadata_hash_mask = retval.metadata_increment - 1;
    retval.probe_distance_shift = control->metadata_hash_bits;
    retval.max_probe_distance = control->max_probe_distance;
    retval.probe_distance = retval.metadata_increment | retval.metadata_hash_mask;
    retval.entry_raw = MVM_str_hash_entries(control) - bucket * retval.entry_size;
    retval.metadata = MVM_str_hash_metadata(control) + bucket;
    return retval;
}

MVM_STATIC_INLINE void *MVM_str_hash_fetch_nocheck(MVMThreadContext *tc,
                                                   MVMStrHashTable *hashtable,
                                                   MVMString *key) {
    if (MVM_str_hash_is_empty(tc, hashtable)) {
        return NULL;
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
                return entry;
            }
        }
        /* There's a sentinel at the end. This will terminate: */
        if (*ls.metadata < ls.probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced further on. Hence, the key
               we seek can't be in the hash table. */
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

MVM_STATIC_INLINE void *MVM_str_hash_fetch(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    return MVM_str_hash_fetch_nocheck(tc, hashtable, want);
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
    return control ? control->cur_items : 0;
}

/* If this returns 0, then you have not yet called MVM_str_hash_build */
MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_entry_size(MVMThreadContext *tc,
                                                          MVMStrHashTable *hashtable) {
    struct MVMStrHashTableControl *control = hashtable->table;
    return control ? control->entry_size : 0;
}
