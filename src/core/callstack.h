/* A region of the call stack, used for call frames that have not escaped to
 * the heap. */
struct MVMCallStackRegion {
    /* Next call stack region, which we start allocating in if this one is
     * full. NULL if none has been allocated yet. */
    MVMCallStackRegion *next;

    /* Previous call stack region, if any. */
    MVMCallStackRegion *prev;

    /* The place we'll allocate the next frame. */
    char *alloc;

    /* The end of the allocatable region. */
    char *alloc_limit;
};

/* The default size of a call stack region. */
#define MVM_CALLSTACK_REGION_SIZE 131072

/* Functions for working with call stack regions. */
void MVM_callstack_region_init(MVMThreadContext *tc);
MVMCallStackRegion * MVM_callstack_region_next(MVMThreadContext *tc);
MVMCallStackRegion * MVM_callstack_region_prev(MVMThreadContext *tc);
void MVM_callstack_reset(MVMThreadContext *tc);
void MVM_callstack_region_destroy_all(MVMThreadContext *tc);
