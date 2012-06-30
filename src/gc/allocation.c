/* Allocation of managed memory - that is, from the memory space that is
 * managed by the garbage collector. Memory that is in turn owned by a
 * GC-able object will be allocated separately and freed explicitly by
 * its REPR gc_free routine. */

#include "moarvm.h"
static void run_gc(MVMThreadContext *tc);
 
/* Allocate the specified amount of memory from the nursery. Will
 * trigger a GC run if there is not enough. */
void * MVM_gc_allocate(MVMThreadContext *tc, size_t size) {
    void *allocated;
    
    /* Guard against 0-byte allocation. */
    if (size > 0) {
        /* Do a GC run if this allocation won't fit in what we have
         * left in the nursery. Note this is a loop to handle a
         * pathological case: all the objects in the nursery are very
         * young and thus survive in the nursery, meaning that no space
         * actually gets freed up. The next run will promote them to the
         * second generation. Note that this circumstance is exceptionally
         * unlikely in any non-contrived situation. */
        while ((char *)tc->nursery_alloc + size >= (char *)tc->nursery_alloc_limit) {
            if (size > MVM_NURSERY_SIZE)
                MVM_panic(MVM_exitcode_gcalloc, "Attempt to allocate more than the maximum nursery size");
            run_gc(tc);
        }
        
        /* Allocate (just bump the pointer). */
        allocated = tc->nursery_alloc;
        tc->nursery_alloc = (char *)tc->nursery_alloc + size;
    }
    else {
        MVM_panic(MVM_exitcode_gcalloc, "Cannot allocate 0 bytes of memory in the nursery");
    }
    
    return allocated;
}

/* Same as MVM_gc_allocate, but explicitly zeroes the memory that is
 * returned. */
void * MVM_gc_allocate_zeroed(MVMThreadContext *tc, size_t size) {
    return MVM_gc_allocate(tc, size);
}

/* Allocates a new STable, based on the specified thread context, REPR
 * and meta-object. */
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, MVMREPROps *repr, MVMObject *how) {
    MVMSTable *st;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&how);
    st               = MVM_gc_allocate_zeroed(tc, sizeof(MVMSTable));
    st->header.flags = MVM_CF_STABLE;
    st->header.owner = tc->thread_id;
    st->REPR         = repr;
    st->HOW          = how;
    MVM_gc_root_temp_pop(tc);
    return st;
}

/* Allocates a new type object. */
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    obj               = MVM_gc_allocate_zeroed(tc, sizeof(MVMObject));
    obj->header.flags = MVM_CF_TYPE_OBJECT;
    obj->header.owner = tc->thread_id;
    obj->st           = st;
    MVM_gc_root_temp_pop(tc);
    return obj;
}

/* Allocates a new object, and points it at the specified STable. */
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    obj               = MVM_gc_allocate_zeroed(tc, st->size);
    obj->header.owner = tc->thread_id;
    obj->st           = st;
    MVM_gc_root_temp_pop(tc);
    return obj;
}

/* Does a garbage collection run. */
static void run_gc(MVMThreadContext *tc) {
    /* Increment GC sequence number. */
    tc->instance->gc_seq_number++;
    
    /* XXX At some point, we need to decide here whether to sweep the
     * second generation too. But since that's NYI, for now we just
     * always collect the first one for now. */
    if (1) {
        /* Do a nursery collection. We record the current tospace allocation
         * pointer to serve as a limit for the later sweep phase. */
        void *limit = tc->nursery_alloc;
        MVM_gc_nursery_collect(tc);
        MVM_gc_nursery_free_uncopied(tc, limit);
    }
}
