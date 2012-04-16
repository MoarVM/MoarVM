#include "moarvm.h"

/* Initializes a new thread context. Note that this doesn't set up a
 * thread itself, it just creates the data structure that exists in
  * MoarVM per thread. */
MVMThreadContext * MVM_tc_create(MVMInstance *instance) {
    MVMThreadContext *tc = calloc(1, sizeof(MVMThreadContext));
    
    /* Associate with VM instance. */
    tc->instance = instance;
    
    /* Set up GC nursery. */
    tc->nursery_fromspace   = calloc(1, MVM_NURSERY_SIZE);
    tc->nursery_tospace     = calloc(1, MVM_NURSERY_SIZE);
    tc->nursery_alloc       = tc->nursery_fromspace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;

    return tc;
}

/* Destroys a given thread context. This will also free the nursery.
 * This means that it must no longer be in use, at all; this can be
 * ensured by a GC run at thread exit that forces evacuation of all
 * objects from this nursery to the second generation. Only after
 * that is true should this be called. */
void MVM_tc_destroy(MVMThreadContext *tc) {
    /* Free the nursery. */
    free(tc->nursery_fromspace);
    free(tc->nursery_tospace);
    
    /* Free the thread context itself. */
    memset(tc, 0, sizeof(MVMThreadContext));
    free(tc);
}
