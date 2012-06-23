#include "moarvm.h"

/* Adds a location holding a collectable object to the permanent list of GC
 * roots, so that it will always be marked and never die. Note that the
 * address of the collectable must be passed, since it will need to be
 * updated. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    if (obj_ref == NULL)
        MVM_panic(0, "Illegal attempt to add null object address as a permanent root");

    if (apr_thread_mutex_lock(tc->instance->mutex_permroots) == APR_SUCCESS) {
        /* Allocate extra permanent root space if needed. */
        if (tc->instance->num_permroots == tc->instance->alloc_permroots) {
            tc->instance->alloc_permroots *= 2;
            tc->instance->permroots = realloc(tc->instance->permroots,
                tc->instance->alloc_permroots);
        }
        
        /* Add this one to the list. */
        tc->instance->permroots[tc->instance->num_permroots] = obj_ref;
        tc->instance->num_permroots++;
        
        if (apr_thread_mutex_unlock(tc->instance->mutex_permroots) != APR_SUCCESS)
            MVM_panic(0, "Unable to unlock GC permanent root mutex");
    }
    else {
        MVM_panic(0, "Unable to lock GC permanent root mutex");
    }
}

/* Adds the set of permanently registered roots to a GC worklist. */
void MVM_gc_root_add_parmanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMuint32         i, num_roots;
    MVMCollectable ***permroots;
    num_roots = tc->instance->num_permroots;
    permroots = tc->instance->permroots;
    for (i = 0; i < num_roots; i++)
        MVM_gc_worklist_add(tc, worklist, permroots[i]);
}
