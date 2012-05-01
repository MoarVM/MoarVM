/* Representation used by VM-level hashes. At the moment, this just
 * uses the APR's built-in hashes, which may or may not turn out to
 * optimal. */
typedef struct _MVMHashBody {
    /* The APR memory pool that will be used by this hash. */
    apr_pool_t *pool;
    
    /* The hash table itself. */
    apr_hash_t *hash;
} MVMHashBody;
typedef struct _MVMHash {
    MVMObject common;
    MVMHashBody body;
} MVMHash;

/* Function for REPR setup. */
MVMREPROps * MVMHash_initialize(MVMThreadContext *tc);
