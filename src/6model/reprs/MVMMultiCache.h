/* Maximum positional arity we cache up to. (Good to make it a power of 2.) */
#define MVM_MULTICACHE_MAX_ARITY    4

/* Maximum entries we cache per arity. (Good to make it a power of 2.) */
#define MVM_MULTICACHE_MAX_ENTRIES  32

/* The cached info that we keep per arity. */
struct MVMMultiArityCache {
    /* The number of entries in the cache. */
    MVMuint8 num_entries;

    /* This is a bunch of type IDs. We allocate it arity * MAX_ENTRIES
     * big and go through it in arity sized chunks. */
    MVMint64 *type_ids;
    
    /* Whether the entry is allowed to have named arguments. Doesn't say
     * anything about which ones, though. Something that is ambivalent
     * about named arguments to the degree it doesn't care about them 
     * even tie-breaking (like NQP) can just throw such entries into the
     * cache. Things that do care should not make such cache entries. */
    MVMuint8 *named_ok;

    /* The results we return from the cache. */
    MVMObject **results;
};

/* Body of a multi-dispatch cache. */
struct MVMMultiCacheBody {
    /* Zero-arity cached result. */
    MVMObject *zero_arity;

    /* The per-arity cache. */
    MVMMultiArityCache arity_caches[MVM_MULTICACHE_MAX_ARITY];
};

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
MVMObject * MVM_multi_cache_find_spesh(MVMThreadContext *tc, MVMObject *cache, MVMSpeshCallInfo *arg_info);
