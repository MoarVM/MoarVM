#include "moarvm.h"
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* Garbage collects the nursery. This is a semi-space copying collector,
 * but only copies very young objects. Once an object is seen/copied once
 * in here (may be tuned in the future to twice or so - we'll see) then it
 * is not copied to tospace, but instead promoted to the second generation,
 * which is managed through mark-compact. Note that it adds the roots and
 * processes them in phases, to try to avoid building up a huge worklist. */
void MVM_gc_nursery_collect(MVMThreadContext *tc) {
    MVMGCWorklist *worklist;
    void *fromspace;
    void *tospace;
    
    /* Swap fromspace and tospace. */
    fromspace = tc->nursery_tospace;
    tospace   = tc->nursery_fromspace;
    tc->nursery_fromspace = fromspace;
    tc->nursery_tospace   = tospace;
    
    /* Reset nursery allocation pointers to the new tospace. */
    tc->nursery_alloc       = tospace;
    tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;
    
    /* Create a GC worklist. */
    worklist = MVM_gc_worklist_create(tc);
    
    /* Add permanent roots and process them. */
    MVM_gc_root_add_parmanents_to_worklist(tc, worklist);
    process_worklist(tc, worklist);
    
    /* Add temporary roots and process them. */
    
    /* Find roots in frames and process them. */
    
    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    printf("Going to process worklist of %d items\n", worklist->items);
    MVM_panic(15, "Out of memory; GC not yet implemented!");
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit) {
}
