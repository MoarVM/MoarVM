/* Public API at the end of the file. */

/* At the moment this is a massively revised version of uthash.h, hence: */

/*
Copyright (c) 2003-2014, Troy D. Hanson    http://troydhanson.github.com/uthash/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Frees the entire contents of the hash, leaving you just the hashtable itself,
   which you allocated (heap, stack, inside another struct, wherever) */
void MVM_str_hash_demolish(MVMThreadContext *tc, MVMStrHashTable *hashtable);
/* and then free memory if you allocated it */

void MVM_str_hash_expand_buckets(MVMThreadContext *tc, MVMStrHashTable *hashtable);

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_str_hash_build(MVMThreadContext *tc, MVMStrHashTable *hashtable, MVMuint32 entry_size) {
    if (MVM_UNLIKELY(entry_size == 0 || entry_size > 1024 || entry_size & 3)) {
        MVM_oops(tc, "Hash table entry_size %" PRIu32 " is invalid", entry_size);
    }
    hashtable->buckets = NULL;
    hashtable->num_buckets = 0;
    hashtable->log2_num_buckets = 0;
    hashtable->num_items = 0;
    hashtable->ideal_chain_maxlen = 0;
    hashtable->nonideal_items = 0;
    hashtable->ineff_expands = 0;
    hashtable->noexpand = 0;
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

/* Fibonacci bucket determination.
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 * If the size of the hashv is changed we will need to change max_hashv_div_phi,
 * to be max_hashv / phi rounded to the nearest *odd* number.
 * max_hashv / phi = 11400714819323198485 */
#define max_hashv_div_phi UINT64_C(11400714819323198485)

MVM_STATIC_INLINE MVMHashBktNum MVM_str_hash_bucket(MVMuint64 hashv, MVMuint32 offset) {
    return (hashv * max_hashv_div_phi) >> ((sizeof(MVMHashv)*8) - offset);
}

MVM_STATIC_INLINE void MVM_str_hash_allocate_buckets(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable) {
    if (MVM_UNLIKELY(hashtable->entry_size == 0))
        MVM_oops(tc, "Hash table entry_size not set");
    if (MVM_UNLIKELY(hashtable->entry_size > 1024 || hashtable->entry_size & 3))
        MVM_oops(tc, "Hash table entry_size %" PRIu32 " is invalid", hashtable->entry_size);

    /* Lazily allocate the hash table. */
    hashtable->buckets = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                                                     HASH_INITIAL_NUM_BUCKETS * sizeof(struct MVMStrHashBucket));
    hashtable->num_buckets = HASH_INITIAL_NUM_BUCKETS;
    hashtable->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
#if HASH_DEBUG_ITER
    hashtable->ht_id = MVM_proc_rand_i(tc);
#endif
}

/* UNCONDITIONALLY creates a new hash entry with the given key, returning the
 * newly allocated entry. Doesn't check if the key already exists. Use with
 * care. */
MVM_STATIC_INLINE void *MVM_str_hash_insert_nt(MVMThreadContext *tc,
                                            MVMStrHashTable *hashtable,
                                            MVMString *key) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        MVM_str_hash_allocate_buckets(tc, hashtable);
    }

    MVMHashv hashv = key->body.cached_hash_code;
    if (!hashv) {
        MVM_string_compute_hash_code(tc, key);
        hashv = key->body.cached_hash_code;
    }
    MVMHashBktNum bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMStrHashBucket *bucket = hashtable->buckets + bucket_num;

    struct MVMStrHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, hashtable->entry_size);
    entry->key = key;

    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
#if HASH_DEBUG_ITER
    ++hashtable->serial;
    hashtable->last_delete_at = ~0;
#endif
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        MVM_str_hash_expand_buckets(tc, hashtable);
    }
    return entry;
}

MVM_STATIC_INLINE void *MVM_str_hash_lvalue_fetch_nt(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable,
                                                     MVMString *key) {
    MVMHashv hashv = key->body.cached_hash_code;
    if (!hashv) {
        MVM_string_compute_hash_code(tc, key);
        hashv = key->body.cached_hash_code;
    }
    MVMHashBktNum bucket_num;
    struct MVMStrHashBucket *bucket;

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        MVM_str_hash_allocate_buckets(tc, hashtable);
        bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
        bucket = hashtable->buckets + bucket_num;
    }
    else {
        bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
        bucket = hashtable->buckets + bucket_num;

        /* This is the same code as the body of fetch below */
        struct MVMStrHashHandle *have = bucket->hh_head;
        /* iterate over items in a known bucket to find desired item */
        /* not adding the shortcut of comparing string cached hash values as it's
         * slightly complex, and this code will die soon */
        while (have) {
            /* DON'T FORGET to fill in the NULL key. Or we go boom here. */
            assert(have->key);
            if (have->key == key
                || (MVM_string_graphs_nocheck(tc, key) == MVM_string_graphs_nocheck(tc, have->key)
                    && MVM_string_substrings_equal_nocheck(tc, key, 0,
                                                           MVM_string_graphs_nocheck(tc, key),
                                                           have->key, 0))) {
                return have;
            }
            have = have->hh_next;
        }
    }

    /* Not found: */

    struct MVMStrHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, hashtable->entry_size);
    entry->key = NULL;

    /* So this is (mostly) the code from insert above. */
    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
#if HASH_DEBUG_ITER
    ++hashtable->serial;
    hashtable->last_delete_at = ~0;
#endif
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        /* OK, this little dance confirms that entry->key of NULL is somewhat
         * hacky. I think that it will be far less hacky with open addressing,
         * because that has to expand *before* it can write in the new entry,
         * whereas this has to add it first, so that the bucket count
         * arithmetic works out. */
        entry->key = key;
        MVM_str_hash_expand_buckets(tc, hashtable);
        entry->key = NULL;
    }

    return entry;
}

/* IIRC Schwern figured that have/want were very good names. */
MVM_STATIC_INLINE void *MVM_str_hash_fetch_nt(MVMThreadContext *tc,
                                              MVMStrHashTable *hashtable,
                                              MVMString *want) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return NULL;
    }

    MVMHashv hashv = want->body.cached_hash_code;
    if (!hashv) {
        MVM_string_compute_hash_code(tc, want);
        hashv = want->body.cached_hash_code;
    }
    MVMHashBktNum bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMStrHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMStrHashHandle *have = bucket->hh_head;
    /* iterate over items in a known bucket to find desired item */
    /* not adding the shortcut of comparing string cached hash values as it's
     * slightly complex, and this code will die soon */
    while (have) {
        if (have->key == want
            || (MVM_string_graphs_nocheck(tc, want) == MVM_string_graphs_nocheck(tc, have->key)
                && MVM_string_substrings_equal_nocheck(tc, want, 0,
                                                       MVM_string_graphs_nocheck(tc, want),
                                                       have->key, 0))) {
            return have;
        }
        have = have->hh_next;
    }
    return NULL;
}


MVM_STATIC_INLINE void MVM_str_hash_delete_nt(MVMThreadContext *tc,
                                              MVMStrHashTable *hashtable,
                                              MVMString *want) {

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return;
    }

    MVMHashv hashv = want->body.cached_hash_code;
    if (!hashv) {
        MVM_string_compute_hash_code(tc, want);
        hashv = want->body.cached_hash_code;
    }
    MVMHashBktNum bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMStrHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMStrHashHandle *have = bucket->hh_head;
    struct MVMStrHashHandle *prev = NULL;
    /* iterate over items in a known bucket to find desired item */
    while (have) {
        if (have->key == want
            || (MVM_string_graphs_nocheck(tc, want) == MVM_string_graphs_nocheck(tc, have->key)
                && MVM_string_substrings_equal_nocheck(tc, want, 0,
                                                       MVM_string_graphs_nocheck(tc, want),
                                                       have->key, 0))) {
            if (prev) {
                prev->hh_next = have->hh_next;
            } else {
                bucket->hh_head = have->hh_next;
            }

            --bucket->count;
            --hashtable->num_items;
#if HASH_DEBUG_ITER
            ++hashtable->serial;
            hashtable->last_delete_at = bucket_num;
#endif

            MVM_fixed_size_free(tc, tc->instance->fsa,  hashtable->entry_size, have);
            return;
        }
        prev = have;
        have = have->hh_next;
    }
    /* Strange. Not in the hash. */
    return;
}

/* This one is private: */
MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_next_bucket(MVMThreadContext *tc,
                                                              MVMStrHashTable *hashtable,
                                                              MVMStrHashIterator iterator) {
    while (++iterator.hi_bucket < hashtable->num_buckets) {
        if ((iterator.hi_current = hashtable->buckets[iterator.hi_bucket].hh_head)) {
            return iterator;
        }
    }
    /* We are at the end. */
    return iterator;
}

/*******************************************************************************
 * Proposed public API is what follows, plus MVM_str_hash_build and
 * MVM_str_hash_demolish. And (probably) the *_nt variants of the calls below,
 * plus MVM_str_hash_insert_nt (with the caveat that it is a footgun and will
 * insert duplicate keys unless you are careful).
 * Maybe we should rename it to MVM_str_hash_footgun. Or blunderbuss. Or gonne.
 ******************************************************************************/

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

/* Iterators. The plan is that MVMStrHashIterator will become a MVMuint64,
 * 32 bits for array index in the open addressing hash, 32 bits for PRNG. */

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
              iterator.hi_bucket == hashtable->last_delete_at))) {
        MVM_oops(tc, "MVM_str_hash_next called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, hashtable->serial);
    }
#endif

    if (MVM_UNLIKELY(iterator.hi_current == NULL)) {
        return iterator;
    }
    if ((iterator.hi_current = iterator.hi_current->hh_next)) {
        return iterator;
    }
    return MVM_str_hash_next_bucket(tc, hashtable, iterator);
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_first(MVMThreadContext *tc,
                                                        MVMStrHashTable *hashtable) {
    MVMStrHashIterator iterator;
    iterator.hi_bucket = 0;

#if HASH_DEBUG_ITER
    iterator.owner = hashtable->ht_id;
    iterator.serial = hashtable->serial;
#endif

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        iterator.hi_current = NULL;
        return iterator;
    }

    if ((iterator.hi_current = hashtable->buckets[0].hh_head)) {
        return iterator;
    }

    return MVM_str_hash_next_bucket(tc, hashtable, iterator);
;
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
    return iterator.hi_current;
}

MVM_STATIC_INLINE MVMStrHashIterator MVM_str_hash_end(MVMThreadContext *tc,
                                                      MVMStrHashTable *hashtable) {
    MVMStrHashIterator iterator;
    iterator.hi_bucket = hashtable->num_buckets;
    iterator.hi_current = NULL;
#if HASH_DEBUG_ITER
    iterator.owner = hashtable->ht_id;
    iterator.serial = hashtable->serial;
#endif
    return iterator;
}

MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_count(MVMThreadContext *tc,
                                                     MVMStrHashTable *hashtable) {
    return hashtable->num_items;
}

/* If this returns 0, then you have not yet called MVM_str_hash_build */
MVM_STATIC_INLINE MVMHashNumItems MVM_str_hash_entry_size(MVMThreadContext *tc,
                                                          MVMStrHashTable *hashtable) {
    return hashtable->entry_size;
}
