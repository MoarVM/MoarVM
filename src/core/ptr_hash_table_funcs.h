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
void MVM_ptr_hash_demolish(MVMThreadContext *tc, MVMPtrHashTable *hashtable);
/* and then free memory if you allocated it */

void MVM_ptr_hash_expand_buckets(MVMThreadContext *tc, MVMPtrHashTable *hashtable);

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_ptr_hash_build(MVMThreadContext *tc, MVMPtrHashTable *hashtable) {
    hashtable->buckets = NULL;
    hashtable->num_buckets = 0;
    hashtable->log2_num_buckets = 0;
    hashtable->num_items = 0;
    hashtable->ideal_chain_maxlen = 0;
    hashtable->nonideal_items = 0;
    hashtable->ineff_expands = 0;
    hashtable->noexpand = 0;
}

/* Fibonacci bucket determination.
 * pointers are not under the control of the external (ab)user, so we don't need
 * a crypographic hash. Moreover, "good enough" is likely better than "perfect"
 * at actual hashing, so until proven otherwise, we'll "just" multiply by the
 * golden ratio and then downshift to get a bucket. This will mix in all the
 * bits of the pointer, so avoids "obvious" problems such as pointers being
 * 8 or 16 byte aligned (lots of zeros in the least significant bits)
 * similarly the potential for lots of repetition in the higher bits.
 *
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 * If the size of the hashv is changed we will need to change max_hashv_div_phi,
 * to be max_hashv / phi rounded to the nearest *odd* number.
 * max_hashv / phi = 11400714819323198485 */

#if 8 <= MVM_PTR_SIZE
MVM_STATIC_INLINE MVMHashBktNum MVM_ptr_hash_bucket(void *ptr, MVMuint32 offset) {
    return (((uintptr_t)ptr) * UINT64_C(11400714819323198485)) >> ((sizeof(uintptr_t)*8) - offset);
}
#else
MVM_STATIC_INLINE MVMHashBktNum MVM_ptr_hash_bucket(void *ptr, MVMuint32 offset) {
    return (((uintptr_t)ptr) * 0x9e3779b7) >> ((sizeof(uintptr_t)*8) - offset);
}
#endif

MVM_STATIC_INLINE void MVM_ptr_hash_allocate_buckets(MVMThreadContext *tc,
                                                     MVMPtrHashTable *hashtable) {
    /* Lazily allocate the hash table. */
    hashtable->buckets = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                                                     HASH_INITIAL_NUM_BUCKETS * sizeof(struct MVMPtrHashBucket));
    hashtable->num_buckets = HASH_INITIAL_NUM_BUCKETS;
    hashtable->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
MVM_STATIC_INLINE void MVM_ptr_hash_insert(MVMThreadContext *tc,
                                           MVMPtrHashTable *hashtable,
                                           void *key,
                                           uintptr_t value) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        MVM_ptr_hash_allocate_buckets(tc, hashtable);
    }

    MVMHashBktNum bucket_num = MVM_ptr_hash_bucket(key, hashtable->log2_num_buckets);
    struct MVMPtrHashBucket *bucket = hashtable->buckets + bucket_num;

    struct MVMPtrHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(struct MVMPtrHashHandle));
    entry->key = key;
    entry->value = value;

    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        MVM_ptr_hash_expand_buckets(tc, hashtable);
    }
}

MVM_STATIC_INLINE struct MVMPtrHashHandle *MVM_ptr_hash_lvalue_fetch(MVMThreadContext *tc,
                                                                     MVMPtrHashTable *hashtable,
                                                                     void *key) {
    MVMHashBktNum bucket_num;
    struct MVMPtrHashBucket *bucket;

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        MVM_ptr_hash_allocate_buckets(tc, hashtable);
        bucket_num = MVM_ptr_hash_bucket(key, hashtable->log2_num_buckets);
        bucket = hashtable->buckets + bucket_num;
    }
    else {
        bucket_num = MVM_ptr_hash_bucket(key, hashtable->log2_num_buckets);
        bucket = hashtable->buckets + bucket_num;

        /* This is the same code as the body of fetch below */
        struct MVMPtrHashHandle *have = bucket->hh_head;
        /* iterate over items in a known bucket to find desired item */
        while (have) {
            /* DON'T FORGET to fill in the NULL key. Or we go boom here. */
            assert(have->key);
            if (have->key == key) {
                return have;
            }
            have = have->hh_next;
        }
    }

    /* Not found: */

    struct MVMPtrHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(struct MVMPtrHashHandle));
    entry->key = NULL;

    /* So this is (mostly) the code from insert above. */
    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        /* OK, this little dance confirms that entry->key of NULL is somewhat
         * hacky. I think that it will be far less hacky with open addressing,
         * because that has to expand *before* it can write in the new entry,
         * whereas this has to add it first, so that the bucket count
         * arithmetic works out. */
        entry->key = key;
        MVM_ptr_hash_expand_buckets(tc, hashtable);
        entry->key = NULL;
    }

    return entry;
}

MVM_STATIC_INLINE struct MVMPtrHashHandle *MVM_ptr_hash_fetch(MVMThreadContext *tc,
                                                              MVMPtrHashTable *hashtable,
                                                              void *key) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return NULL;
    }

    MVMHashBktNum bucket_num = MVM_ptr_hash_bucket(key, hashtable->log2_num_buckets);
    struct MVMPtrHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMPtrHashHandle *have = bucket->hh_head;
    /* iterate over items in a known bucket to find desired item */
    while (have) {
        if (have->key == key) {
            return have;
        }
        have = have->hh_next;
    }
    return NULL;
}

MVM_STATIC_INLINE uintptr_t MVM_ptr_hash_fetch_and_delete(MVMThreadContext *tc,
                                                          MVMPtrHashTable *hashtable,
                                                          void *key) {

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return 0;
    }

    MVMHashBktNum bucket_num = MVM_ptr_hash_bucket(key, hashtable->log2_num_buckets);
    struct MVMPtrHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMPtrHashHandle *have = bucket->hh_head;
    struct MVMPtrHashHandle *prev = NULL;
    /* iterate over items in a known bucket to find desired item */
    while (have) {
        if (have->key == key) {
            if (prev) {
                prev->hh_next = have->hh_next;
            } else {
                bucket->hh_head = have->hh_next;
            }

            --bucket->count;
            --hashtable->num_items;

            uintptr_t value = have->value;
            MVM_fixed_size_free(tc, tc->instance->fsa,  sizeof(struct MVMPtrHashHandle), have);
            return value;
        }
        prev = have;
        have = have->hh_next;
    }
    /* Strange. Not in the hash. */
    return 0;
}
