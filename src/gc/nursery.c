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
    
    /* Add things that are roots for the first generation because
     * they are pointed to by objects in the second generation and
     * process them. */
    
    /* Find roots in frames and process them. */
    
    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMCollectable **item_ptr;
    while (item_ptr = MVM_gc_worklist_get(tc, worklist)) {
        /* Dereference the object we're considering. */
        MVMCollectable *item = *item_ptr;

        /* If the item is NULL, that's fine - it's just a null reference and
         * thus we've no object to consider. */
        if (item == NULL)
            continue;

        /* If it's in the second generation, we have nothing to do. */
        if (item->flags & MVM_CF_SECOND_GEN)
            continue;
        
        /* If we already saw the item and copied it, then it will have a
         * forwarding address already. Just update this pointer to the
         * new address and we're done. */
        if (item->forwarder) {
            *item_ptr = item->forwarder;
            continue;
        }
        
        /* If we saw it in the nursery before, then we will promote it
         * to the second generation. */
        if (item->flags & MVM_CF_NURSERY_SEEN) {
            MVM_panic(15, "Promotion to second generation in NYI!");
            continue;
        }
        
        /* Otherwise, we need to do the copy. What sort of thing are we
         * going to copy? */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* It's an object instance. Get the size from the STable. */
            
            /* Copy it to tospace and set the forwarding pointer. */
            
            /* Set the "seen in nursery" flag. */
            
            /* If needed, mark it. This will add addresses to the worklist
             * that will need updating. Note that we are passing the address
             * of the object *after* copying it since those are the addresses
             * we care about updating; the old chunk of memory is now dead! */
             
            MVM_panic(15, "Can't copy objects in the GC yet");
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            MVM_panic(15, "Can't copy type objects in the GC yet");
        }
        else if (item->flags & MVM_CF_STABLE) {
            MVM_panic(15, "Can't copy stables in the GC yet");
        }
        else if (item->flags & MVM_CF_SC) {
            MVM_panic(15, "Can't copy serialization contexts in the GC yet");
        }
    }
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit) {
}
