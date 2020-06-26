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
void MVM_index_hash_demolish(MVMThreadContext *tc, MVMIndexHashTable *hashtable);
/* and then free memory if you allocated it */

void MVM_index_hash_expand_buckets(MVMThreadContext *tc, MVMIndexHashTable *hashtable, MVMString **list);

/* Call this before you use the hashtable, to initialise it.
 * Doesn't allocate memory - you can embed the struct within a larger struct if
 * you wish.
 */
MVM_STATIC_INLINE void MVM_index_hash_build(MVMIndexHashTable *hashtable) {
    hashtable->buckets = NULL;
    hashtable->num_buckets = 0;
    hashtable->log2_num_buckets = 0;
    hashtable->num_items = 0;
    hashtable->ideal_chain_maxlen = 0;
    hashtable->nonideal_items = 0;
    hashtable->ineff_expands = 0;
    hashtable->noexpand = 0;
}

MVM_STATIC_INLINE void MVM_index_hash_store_nt(MVMThreadContext *tc,
                                               MVMIndexHashTable *hashtable,
                                               MVMString **list,
                                               MVMuint32 idx) {
    MVMString *key = list[idx];

    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        /* Lazily allocate the hash table. */
        hashtable->buckets = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                                                         HASH_INITIAL_NUM_BUCKETS * sizeof(struct MVMIndexHashBucket));
        hashtable->num_buckets = HASH_INITIAL_NUM_BUCKETS;
        hashtable->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
    }

    MVMHashv hashv = MVM_string_hash_code(tc, key);
    MVMHashBktNum bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMIndexHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMIndexHashHandle *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa,
                                                            sizeof(struct MVMIndexHashHandle));
    entry->index = idx;
    entry->hh_next = bucket->hh_head;
    bucket->hh_head = entry;
    ++hashtable->num_items;
    if (MVM_UNLIKELY(++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
                     && hashtable->noexpand != 1)) {
        MVM_index_hash_expand_buckets(tc, hashtable, list);
    }
}

/* IIRC Schwern figured that have/want were very good names. */
MVM_STATIC_INLINE MVMuint32 MVM_index_hash_fetch_nt(MVMThreadContext *tc,
                                                    MVMIndexHashTable *hashtable,
                                                    MVMString **list,
                                                    MVMString *want) {
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0)) {
        return MVM_INDEX_HASH_NOT_FOUND;
    }

    MVMHashv hashv = MVM_string_hash_code(tc, want);
    MVMHashBktNum bucket_num = MVM_str_hash_bucket(hashv, hashtable->log2_num_buckets);
    struct MVMIndexHashBucket *bucket = hashtable->buckets + bucket_num;
    struct MVMIndexHashHandle *have = bucket->hh_head;
    /* iterate over items in a known bucket to find desired item */
    /* not adding the shortcut of comparing string cached hash values as it's
     * slightly complex, and this code will die soon */
    while (have) {
        MVMString *key = list[have->index];
        if (key == want
            || (MVM_string_graphs_nocheck(tc, want) == MVM_string_graphs_nocheck(tc, key)
                && MVM_string_substrings_equal_nocheck(tc, want, 0,
                                                       MVM_string_graphs_nocheck(tc, want),
                                                       key, 0))) {
            return have->index;
        }
        have = have->hh_next;
    }
    return MVM_INDEX_HASH_NOT_FOUND;
}

MVM_STATIC_INLINE MVMuint32 MVM_index_hash_fetch(MVMThreadContext *tc,
                                                 MVMIndexHashTable *hashtable,
                                                 MVMString **list,
                                                 MVMString *want) {
    if (!MVM_str_hash_key_is_valid(tc, want)) {
        MVM_str_hash_key_throw_invalid(tc, want);
    }
    return MVM_index_hash_fetch_nt(tc, hashtable, list, want);
}
