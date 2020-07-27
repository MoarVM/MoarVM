/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_str_hash_demolish(MVMThreadContext *tc, MVMStrHashTable *hashtable);
/* and then free memory if you allocated it */

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_str_hash_build(MVMThreadContext *tc, MVMStrHashTable *hashtable, MVMuint32 entry_size) {
    if (MVM_UNLIKELY(entry_size == 0 || entry_size > 255 || entry_size & 3)) {
        MVM_oops(tc, "Hash table entry_size %" PRIu32 " is invalid", entry_size);
    }
    memset(hashtable, 0, sizeof(*hashtable));
    hashtable->entry_size = entry_size;
#if HASH_DEBUG_ITER
    /* Given that we can embed the hashtable structure into other structures
     * (such as MVMHash) and those enclosing structures can be moved (GC!) we
     * can't use the address of this structure as its ID for debugging. We
     * could use the address of the first buckets array that we allocate, but if
     * we grow, then that memory could well be re-used for another hashtable,
     * and then we have two hashtables with the same ID, which rather defeats
     * the need to have (likely to be) unique IDs, to spot iterator leakage. */
    hashtable->ht_id = 0;
    hashtable->serial = 0;
    hashtable->last_delete_at = ~0;
#endif
}

MVM_STATIC_INLINE MVMuint64 MVM_str_hash_code(MVMThreadContext *tc,
                                              MVMuint64 salt,
                                              MVMString *key) {
    return (MVM_string_hash_code(tc, key) ^ salt) * UINT64_C(11400714819323198485);
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
void *MVM_str_hash_insert_nt(MVMThreadContext *tc,
                             MVMStrHashTable *hashtable,
                             MVMString *key);

MVM_STATIC_INLINE void *MVM_str_hash_fetch_nt(MVMThreadContext *tc,
                                              MVMStrHashTable *hashtable,
                                              MVMString *key) {
    if (MVM_UNLIKELY(hashtable->entries == NULL)) {
        return NULL;
    }
    unsigned int probe_distance = 1;
    MVMHashNumItems bucket = MVM_str_hash_code(tc, hashtable->salt, key) >> hashtable->key_right_shift;
    char *entry_raw = hashtable->entries + bucket * hashtable->entry_size;
    MVMuint8 *metadata = hashtable->metadata + bucket;
    while (1) {
        if (*metadata == probe_distance) {
            struct MVMStrHashHandle *entry = (struct MVMStrHashHandle *) entry_raw;
            if (entry->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, entry->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           entry->key, 0))) {
                return entry;
            }
        }
        /* There's a sentinel at the end. This will terminate: */
        if (*metadata < probe_distance) {
            /* So, if we hit 0, the bucket is empty. "Not found".
               If we hit something with a lower probe distance then...
               consider what would have happened had this key been inserted into
               the hash table - it would have stolen this slot, and the key we
               find here now would have been displaced futher on. Hence, the key
               we seek can't be in the hash table. */
            return NULL;
        }
        ++probe_distance;
        ++metadata;
        entry_raw += hashtable->entry_size;
        assert(probe_distance <= MVM_HASH_MAX_PROBE_DISTANCE);
        assert(metadata < hashtable->metadata + hashtable->official_size + hashtable->max_items);
        assert(metadata < hashtable->metadata + hashtable->official_size + 256);
    }
}

/* Looks up entry for key, creating it if necessary.
 * Returns the structure we indirect to.
 * If it's freshly allocated, then *entry is NULL (you need to fill this in)
 * and everything else is uninitialised.
 * This might seem like a quirky API, but it's intended to fill a common pattern
 * we have, and the use of NULL key avoids needing two return values.
 * DON'T FORGET to fill in the NULL key. */
void *MVM_str_hash_lvalue_fetch_nt(MVMThreadContext *tc,
                                   MVMStrHashTable *hashtable,
                                   MVMString *key);

void MVM_str_hash_delete_nt(MVMThreadContext *tc,
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
    return MVM_str_hash_lvalue_fetch_nt(tc, hashtable, key);
}

MVM_STATIC_INLINE void *MVM_str_hash_fetch(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    return MVM_str_hash_fetch_nt(tc, hashtable, want);
}

MVM_STATIC_INLINE void MVM_str_hash_delete(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    MVM_str_hash_delete_nt(tc, hashtable, want);
}

/* This is not part of the public API, and subject to change at any point.
 * (possibly in ways that are actually incompatible but won't generate compiler
 * warnings.)
 * Currently 0 is a check with no display, and it always returns the error
 * count. */
enum {
    MVM_HASH_FICK_DISPLAY_NONE      = 0x00,
    MVM_HASH_FSCK_DISPLAY_ERRORS    = 0x01,
    MVM_HASH_FSCK_DISPLAY_ALL       = 0x02,
    MVM_HASH_FSCK_PREFIX_HASHES     = 0x04,
    MVM_HASH_FSCK_KEY_VIA_API       = 0x08, /* not just ASCII keys, might deadlock */
    MVM_HASH_FSCK_CHECK_FROMSPACE   = 0x10  /* O(n) test. */
};

MVMuint64 MVM_str_hash_fsck(MVMThreadContext *tc, MVMStrHashTable *hashtable, MVMuint32 mode);


/* This is private. We need it out here for the inline functions. Use them. */
MVM_STATIC_INLINE MVMuint32 MVM_str_hash_kompromat(MVMStrHashTable *hashtable) {
    return hashtable->official_size + hashtable->probe_overflow_size;
}

/* iterators are stored as unsigned values, metadata index plus one.
 * This is clearly an internal implementation detail. Don't cheat.
 */
MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_next(MVMThreadContext *tc,
                                                       MVMStrHashTable *hashtable,
                                                       MVMStrHashIterator iterator) {
#if HASH_DEBUG_ITER
    if (iterator.owner != hashtable->ht_id) {
        MVM_oops(tc, "MVM_str_hash_next called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, hashtable->ht_id);
    }
    /* "the usual case" is that the iterator serial number  matches the hash
     * serial number.
     * As we permit deletes at the current iterator, we also track whether the
     * last mutation on the hash was a delete, and if so record where. Hence,
     * if the hash serial has advanced by one, and the last delete was at this
     * iterator's current bucket position, that's OK too. */
    if (!(iterator.serial == hashtable->serial
          || (iterator.serial == hashtable->serial - 1 &&
              iterator.pos == hashtable->last_delete_at))) {
        MVM_oops(tc, "MVM_str_hash_next called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, hashtable->serial);
    }
#endif

    if (iterator.pos == 0) {
        MVM_oops(tc, "Calling str_hash_next when iterator is already at the end");
    }

    /* Whilst this looks like it can be optimised to word at a time skip ahead.
     * (Beware of endianness) it isn't easy *yet*, because one can overrun the
     * allocated buffer, and that makes ASAN very excited. */
    while (--iterator.pos > 0) {
        if (hashtable->metadata[iterator.pos - 1]) {
            return iterator;
        }
    }
    return iterator;
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_first(MVMThreadContext *tc,
                                                        MVMStrHashTable *hashtable) {

    if (MVM_UNLIKELY(!hashtable->entry_size || hashtable->entries == NULL)) {
        /* Bother. Both clauses above seems to be part of our de-facto API. */
        return MVM_str_hash_end(tc, hashtable);
    }

    MVMStrHashIterator iterator;
    iterator.pos = MVM_str_hash_kompromat(hashtable);

#if HASH_DEBUG_ITER
    iterator.owner = hashtable->ht_id;
    iterator.serial = hashtable->serial;
#endif

    if (hashtable->metadata[iterator.pos - 1]) {
        return iterator;
    }
    return MVM_str_hash_next(tc, hashtable, iterator);
}

/* FIXME - this needs a better name: */
MVM_STATIC_INLINE void *MVM_str_hash_current(MVMThreadContext *tc,
                                             MVMStrHashTable *hashtable,
                                             MVMStrHashIterator iterator) {
#if HASH_DEBUG_ITER
    if (iterator.owner != hashtable->ht_id) {
        MVM_oops(tc, "MVM_str_hash_current called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, hashtable->ht_id);
    }
    if (iterator.serial != hashtable->serial) {
        MVM_oops(tc, "MVM_str_hash_current called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, hashtable->serial);
    }
#endif

    if (MVM_UNLIKELY(MVM_str_hash_at_end(tc, hashtable, iterator))) {
        /* Bother. This seems to be part of our de-facto API. */
        return NULL;
    }
    assert(hashtable->metadata[iterator.pos - 1]);
    return hashtable->entries + hashtable->entry_size * (iterator.pos - 1);
}

MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_count(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable) {
    return hashtable->cur_items;
}

/* If this returns 0, then you have not yet called MVM_str_hash_build */
MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_entry_size(MVMThreadContext *tc,
                                                          MVMStrHashTable *hashtable) {
    return hashtable->entry_size;
}
