/* The multi-dispatch cache is a set of trees keyed on the address of
 * an interned callsite. The tree is represented as  an array of triples,
 * each having the form (action, match, no-match). The match and no-match
 * are either:
 *   * Positive, and an index into the array for what to check next
 *   * Zero, meaning we failed to find a match
 *   * Negative, meaning we found a match, and should negate the index
 *     to get a the resulting candidate
 *
 * The first MVM_MULTICACHE_HASH_SIZE entries in the array are tree roots.
 * They are all set to have a NULL callsite matcher when there's no tree
 * there, implying an immediate match failure.
 *
 * The matcher starts in callsite match mode, meaning that the matcher is
 * the memory address of a callsite. This naturally handles hash collisions.
 *
 * Once we have a matching callsite, it flips into argument matching mode.
 * The lowermost bits of the action represent the index into the arguments
 * buffer for the argument we need to test. The next bit is for concrete or
 * not. The bit after it is rw or not. The remaining bits correspond to the
 * STable's type cache ID.
 *
 * The construction of the tree is such that we only have to test the first
 * argument until we get a match, then the second, etc. This means that common
 * prefixes are factored out, keeping the tree smaller. The use of a single
 * block of memory is also aimed at getting good CPU cache hit rates.
 *
 * The tree array is immutable, and so can safely be read by many threads, and
 * kept in thier CPU caches. Upon a new entry, the cache will be copied, and the
 * tweaks made. The cache head pointer will then be set to the new cache, and the
 * old cache memory scheduled for freeeing at the next safepoint.
 */

/* A node in the cache. */
struct MVMMultiCacheNode {
    union {
        MVMCallsite *cs;
        MVMuint64 arg_match;
    } action;
    MVMint32 match;
    MVMint32 no_match;
};

/* Body of a multi-dispatch cache. */
struct MVMMultiCacheBody {
    /* Pointer to the an array of nodes, which we can initially index
     * into using a hahsed calsite. Replaced in whole whenever there is
     * a change. */
    MVMMultiCacheNode *node_hash_head;

    /* Array of results we may return from the cache. Note that this is
     * replaced entirely whenever we update the cache. It is append only,
     * and so will be valid for older versions of the cache too. We must
     * replace this and do a memory barrier before replacing node_hash_head
     * with its new version on update. Conversely, readers must read the
     * node_hash_head and *then* read results here, so it will always have
     * been udpated in time. */
    MVMObject **results;

    /* The number of results, so we can GC mark and free. */
    size_t num_results;

    /* The amount of memory the cache uses. Used for freeing with the fixed
     * size allocator. */
    size_t cache_memory_size;
};

/* Hash table size. Must be a power of 2. */
#define MVM_MULTICACHE_HASH_SIZE    8
#define MVM_MULTICACHE_HASH_FILTER  (MVM_MULTICACHE_HASH_SIZE - 1)

struct MVMMultiCache {
    MVMObject common;
    MVMMultiCacheBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMMultiCache_initialize(MVMThreadContext *tc);

/* Functions relating to multi-dispatch cache usage. */
MVMObject * MVM_multi_cache_add(MVMThreadContext *tc, MVMObject *cache, MVMObject *capture, MVMObject *result);
MVMObject * MVM_multi_cache_find(MVMThreadContext *tc, MVMObject *cache, MVMObject *capture);
MVMObject * MVM_multi_cache_find_callsite_args(MVMThreadContext *tc, MVMObject *cache,
    MVMCallsite *cs, MVMRegister *args);
MVMObject * MVM_multi_cache_find_spesh(MVMThreadContext *tc, MVMObject *cache, MVMSpeshCallInfo *arg_info, MVMSpeshStatsType *type_tuple);
