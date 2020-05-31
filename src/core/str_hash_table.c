#include "moar.h"

void MVM_str_hash_demolish(MVMThreadContext *tc, MVMStrHashTable *hashtable) {
    /* Never allocated? (or already demolished?) */
    if (MVM_UNLIKELY(hashtable->log2_num_buckets == 0))
        return;

    struct MVMStrHashBucket *bucket = hashtable->buckets;
    const struct MVMStrHashBucket *const bucket_end
        = hashtable->buckets + hashtable->num_buckets;

    do {
        struct MVMStrHashHandle *head = bucket->hh_head;
        while (head) {
            struct MVMStrHashHandle *next = head->hh_next;
            MVM_fixed_size_free(tc, tc->instance->fsa, hashtable->entry_size, head);
            head = next;
        }
    } while (++bucket < bucket_end);

    MVM_fixed_size_free(tc, tc->instance->fsa,
                        hashtable->num_buckets*sizeof(struct MVMStrHashBucket),
                        hashtable->buckets);
    /* We shouldn't need either of these, but make something foolproof and they
       invent a better fool: */
    hashtable->buckets = NULL;
    hashtable->log2_num_buckets = 0;
    hashtable->num_items = 0;
}

/* Bucket expansion has the effect of doubling the number of buckets
 * and redistributing the items into the new buckets. Ideally the
 * items will distribute more or less evenly into the new buckets
 * (the extent to which this is true is a measure of the quality of
 * the hash function as it applies to the key domain).
 *
 * With the items distributed into more buckets, the chain length
 * (item count) in each bucket is reduced. Thus by expanding buckets
 * the hash keeps a bound on the chain length. This bounded chain
 * length is the essence of how a hash provides constant time lookup.
 *
 * The calculation of tbl->ideal_chain_maxlen below deserves some
 * explanation. First, keep in mind that we're calculating the ideal
 * maximum chain length based on the *new* (doubled) bucket count.
 * In fractions this is just n/b (n=number of items,b=new num buckets).
 * Since the ideal chain length is an integer, we want to calculate
 * ceil(n/b). We don't depend on floating point arithmetic in this
 * hash, so to calculate ceil(n/b) with integers we could write
 *
 *      ceil(n/b) = (n/b) + ((n%b)?1:0)
 *
 * and in fact a previous version of this hash did just that.
 * But now we have improved things a bit by recognizing that b is
 * always a power of two. We keep its base 2 log handy (call it lb),
 * so now we can write this with a bit shift and logical AND:
 *
 *      ceil(n/b) = (n>>lb) + ( (n & (b-1)) ? 1:0)
 *
 */
void MVM_str_hash_expand_buckets(MVMThreadContext *tc, MVMStrHashTable *tbl) {
    MVMHashBktNum he_bkt;
    MVMHashBktNum he_bkt_i;
    struct MVMStrHashHandle *he_thh, *_he_hh_nxt;
    struct MVMStrHashBucket *he_new_buckets, *_he_newbkt;
    MVMHashBktNum new_num_bkts = tbl->num_buckets * 2;
    MVMHashUInt new_log2_num_buckets = tbl->log2_num_buckets + 1;
    he_new_buckets =
        MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                                    new_num_bkts * sizeof(struct MVMStrHashBucket));
    tbl->ideal_chain_maxlen =
        (tbl->num_items >> new_log2_num_buckets) +
        ((tbl->num_items & (new_num_bkts-1)) ? 1 : 0);
    tbl->nonideal_items = 0;
    /* Iterate the buckets */
    for(he_bkt_i = 0; he_bkt_i < tbl->num_buckets; he_bkt_i++) {
        he_thh = tbl->buckets[ he_bkt_i ].hh_head;
        /* Iterate items in the bucket */
        while (he_thh) {
            _he_hh_nxt = he_thh->hh_next;
            he_bkt = MVM_str_hash_bucket(he_thh->key->body.cached_hash_code, new_log2_num_buckets);
            _he_newbkt = &(he_new_buckets[ he_bkt ]);
            if (++(_he_newbkt->count) > tbl->ideal_chain_maxlen) {
                tbl->nonideal_items++;
                _he_newbkt->expand_mult = _he_newbkt->count /
                                          tbl->ideal_chain_maxlen;
           }
           he_thh->hh_next = _he_newbkt->hh_head;
           _he_newbkt->hh_head = he_thh;
           he_thh = _he_hh_nxt;
        }
    }
    MVM_fixed_size_free(tc, tc->instance->fsa,
                        tbl->num_buckets*sizeof(struct MVMStrHashBucket),
                        tbl->buckets);
    tbl->num_buckets = new_num_bkts;
    tbl->log2_num_buckets = new_log2_num_buckets;
    tbl->buckets = he_new_buckets;
    tbl->ineff_expands = (tbl->nonideal_items > (tbl->num_items >> 1))
        ? (tbl->ineff_expands+1)
        : 0;
    if (tbl->ineff_expands > 1) {
        tbl->noexpand=1;
    }
}
