#ifndef UTHASH_TYPES
#define UTHASH_TYPES
/* If set to 1, will throw if you add a key to the hash during iteration. */
#define MVM_HASH_THROW_ON_ITER_AFTER_ADD_KEY 0
/* If set to 1, will randomize bucket iteration order and bucket insertion order.
 * HASH_ITER_FAST is not affected by iteration order randomization. */
#define MVM_HASH_RANDOMIZE 1
typedef MVMuint64 MVMHashv;
typedef MVMuint32 MVMHashUInt;
typedef MVMHashUInt MVMHashBktNum;
typedef MVMHashUInt MVMHashKeyLen;
typedef MVMHashUInt MVMHashNumItems;
typedef struct UT_hash_bucket {
   struct UT_hash_handle *hh_head;
   MVMHashNumItems count;

   /* expand_mult is normally set to 0. In this situation, the max chain length
    * threshold is enforced at its default value, HASH_BKT_CAPACITY_THRESH. (If
    * the bucket's chain exceeds this length, bucket expansion is triggered).
    * However, setting expand_mult to a non-zero value delays bucket expansion
    * (that would be triggered by additions to this particular bucket)
    * until its chain length reaches a *multiple* of HASH_BKT_CAPACITY_THRESH.
    * (The multiplier is simply expand_mult+1). The whole idea of this
    * multiplier is to reduce bucket expansions, since they are expensive, in
    * situations where we know that a particular bucket tends to be overused.
    * It is better to let its chain length grow to a longer yet-still-bounded
    * value, than to do an O(n) bucket expansion too often.
    */
   MVMHashUInt expand_mult;
} UT_hash_bucket;

#if 8 <= MVM_PTR_SIZE
   typedef MVMuint64 MVM_UT_bucket_rand;
#else
   typedef MVMuint32 MVM_UT_bucket_rand;
#endif

typedef struct UT_hash_table {
   UT_hash_bucket *buckets;
   MVMHashBktNum num_buckets;
   MVMHashUInt log2_num_buckets;
   MVMHashNumItems num_items;
   ptrdiff_t hho; /* hash handle offset (byte pos of hash handle in element */

   /* in an ideal situation (all buckets used equally), no bucket would have
    * more than ceil(#items/#buckets) items. that's the ideal chain length. */
   MVMHashUInt ideal_chain_maxlen;

   /* nonideal_items is the number of items in the hash whose chain position
    * exceeds the ideal chain maxlen. these items pay the penalty for an uneven
    * hash distribution; reaching them in a chain traversal takes >ideal steps */
   MVMHashUInt nonideal_items;

   /* ineffective expands occur when a bucket doubling was performed, but
    * afterward, more than half the items in the hash had nonideal chain
    * positions. If this happens on two consecutive expansions we inhibit any
    * further expansion, as it's not helping; this happens when the hash
    * function isn't a good fit for the key domain. When expansion is inhibited
    * the hash will still work, albeit no longer in constant time. */
   MVMHashUInt ineff_expands;
   MVMHashUInt noexpand;
#if MVM_HASH_RANDOMIZE
   MVM_UT_bucket_rand bucket_rand;
#  if MVM_HASH_THROW_ON_ITER_AFTER_ADD_KEY
   MVMuint64 bucket_rand_last;
#  endif
#endif
} UT_hash_table;

typedef struct UT_hash_handle {
   struct UT_hash_table  *tbl;
   struct UT_hash_handle *hh_next;   /* next hh in bucket order        */
   void *key;                        /* ptr to enclosing struct's key (char * for
                                      * low-level hashes, MVMString * for high level
                                      * hashes) */
   MVMHashKeyLen keylen;             /* enclosing struct's key len     */
   MVMHashv      hashv;              /* result of hash-fcn(key)        */
} UT_hash_handle;
#endif
