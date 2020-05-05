/* Inline cache storage is used for instructions that want to cache data as
 * they are interpreted. This is primarily used by the dispatch instructions
 * in order to store guards, but getlexstatic_o also emits a super-simple
 * guard tree too which just has an instruction to return the value.
 *
 * Each initialized static frame may have inline cache storage. This is not
 * really inline (since we mmap bytecode), but stored as an array off to the
 * side. Lookups are based on the instruction offset into the instruction.
 * It would be very memory intensive to have an entry per byte, so instead we
 * look at the minimum distance between instructions that might use the cache
 * during frame setup, then pick the power of 2 below that as the interval.
 * We can thus do a lookup in the inline cache by doing a bit shift on the
 * current instruction address. */

/* This is the top level cache struct, living in a static frame. */
struct MVMDispInlineCache {
    /* Cache entries. Atomically updated, released via. safepoint. These are
     * always initialized for instructions that would use them to the initial
     * entry for that kind of instruction (in PIC parlance, "unlinked"). */
    MVMDispInlineCacheEntry **entries;

    /* The number of entries, used when we need to GC-walk them. */
    MVMuint32 num_entries;

    /* The bit shift we should do on the instruction address in order to
     * find an entry for a instruciton. */
    MVMuint32 bit_shift;
};

/* We always invoke an action using the cache by calling a function pointer.
 * These are the kinds of pointer we have: one for getlexstatic, another for
 * dispatch. */
typedef MVMObject * MVMDispInlineCacheRunGetLexStatic(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMString *name);
typedef void MVMDispInlineCacheRunDispatch(MVMThreadContext *tc,
        MVMDispInlineCacheEntry **entry_ptr, MVMCallsite *cs,
        MVMuint16 *arg_indices);

/* The baseline inline cache entry. These always start with a pointer to
 * invoke to reach the handler. */
struct MVMDispInlineCacheEntry {
    /* The callback to run when we reach this location. */
    union {
        MVMDispInlineCacheRunGetLexStatic *run_getlexstatic;
        MVMDispInlineCacheRunDispatch *run_dispatch;
    };
};

/* A resolved entry for getlexstatic. */
struct MVMDispInlineCacheEntryResolvedGetLexStatic {
    MVMDispInlineCacheEntry base;
    MVMObject *result;
};

void MVM_disp_inline_cache_setup(MVMThreadContext *tc, MVMStaticFrame *sf);
void MVM_disp_inline_cache_mark(MVMThreadContext *tc, MVMDispInlineCache *cache,
        MVMGCWorklist *worklist);
void MVM_disp_inline_cache_destroy(MVMThreadContext *tc, MVMDispInlineCache *cache);
