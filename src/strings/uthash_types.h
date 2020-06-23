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

#if 8 <= MVM_PTR_SIZE
   typedef MVMuint64 MVM_UT_bucket_rand;
#else
   typedef MVMuint32 MVM_UT_bucket_rand;
#endif

#endif
