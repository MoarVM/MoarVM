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
void MVM_uni_hash_demolish(MVMThreadContext *tc, MVMUniHashTable *hashtable);
/* and then free memory if you allocated it */

void MVM_uni_hash_expand_buckets(MVMThreadContext *tc, MVMUniHashTable *hashtable);

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_uni_hash_build(MVMThreadContext *tc, MVMUniHashTable *hashtable) {
    hashtable->buckets = NULL;
    hashtable->num_buckets = 0;
    hashtable->log2_num_buckets = 0;
    hashtable->num_items = 0;
    hashtable->ideal_chain_maxlen = 0;
    hashtable->nonideal_items = 0;
    hashtable->ineff_expands = 0;
    hashtable->noexpand = 0;
}

/* Unicode names (etc) are not under the control of an external attacker [:-)]
 * so we don't need a cryptographic hash function here. I'm assuming that
 * 32 bit FNV1a is good enough and fast enough to be useful. */
MVM_STATIC_INLINE MVMuint32 MVM_uni_hash_code(const char *key, size_t len) {
    const char *const end = key + len;
    MVMuint32 hash = 0x811c9dc5;
    while (key < end) {
        hash ^= *key++;
        hash *= 0x01000193;
    }
    return hash;
}

/* Fibonacci bucket determination.
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 */

MVM_STATIC_INLINE MVMHashBktNum MVM_uni_hash_bucket(MVMuint32 hashv, MVMuint32 offset) {
    return (hashv * 0x9e3779b7) >> ((sizeof(MVMuint32)*8) - offset);
}

MVM_STATIC_INLINE void MVM_uni_hash_allocate_buckets(MVMThreadContext *tc,
                                                     MVMUniHashTable *hashtable) {
    /* Lazily allocate the hash table. */
    hashtable->buckets = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                                                     HASH_INITIAL_NUM_BUCKETS * sizeof(struct MVMUniHashBucket));
    hashtable->num_buckets = HASH_INITIAL_NUM_BUCKETS;
    hashtable->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
}

/* UNCONDITIONALLY creates a new hash entry with the given key and value.
 * Doesn't check if the key already exists. Use with care. */
MVM_STATIC_INLINE void MVM_uni_hash_insert(MVMThreadContext *tc,
                                           MVMUniHashTable *hashtable,
                                           const char *key,
                                           MVMint32 value) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        MVM_uni_hash_allocate_buckets(tc, hashtable);
    }

    size_t len = strlen(key);
    MVMuint32 hashv = MVM_uni_hash_code(key, len);
    MVMHashBktNum bucket_num = MVM_uni_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMUniHashBucket *bucket = hashtable->buckets + bucket_num;

    struct MVMUniHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(struct MVMUniHashHandle));
    entry->key = key;
    entry->hash = hashv;
    entry->value = value;

    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        MVM_uni_hash_expand_buckets(tc, hashtable);
    }
}

MVM_STATIC_INLINE struct MVMUniHashHandle *MVM_uni_hash_fetch(MVMThreadContext *tc,
                                                              MVMUniHashTable *hashtable,
                                                              const char *key) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return NULL;
    }

    size_t len = strlen(key);
    MVMuint32 hashv = MVM_uni_hash_code(key, len);
    MVMHashBktNum bucket_num = MVM_uni_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMUniHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMUniHashHandle *have = bucket->hh_head;
    /* iterate over items in a known bucket to find desired item */
    while (have) {
        if (have->hash == hashv && 0 == strcmp(have->key, key)) {
            return have;
        }
        have = have->hh_next;
    }
    return NULL;
}

MVM_STATIC_INLINE MVMHashNumItems MVM_uni_hash_count(MVMThreadContext *tc,
                                                     MVMUniHashTable *hashtable) {
    return hashtable->num_items;
}
