/* Representation used by VM-level hashes. At the moment, this just
 * uses the APR's built-in hashes, which will not really be a good
 * long-term ideal, but will get us going for now. */
typedef struct _MVMHashBody {
    /* The APR memory pool that will be used by this hash. */
    apr_pool_t *pool;
    
    /* The hash table itself. Note we need to keep keys and values
     * around, which means we need two APR hashes (this is one of the
     * big reasons we'd be better with something custom). */
    apr_hash_t *key_hash;
    apr_hash_t *value_hash;
} MVMHashBody;
typedef struct _MVMHash {
    MVMObject common;
    MVMHashBody body;
} MVMHash;

/* Function for REPR setup. */
MVMREPROps * MVMHash_initialize(MVMThreadContext *tc);
