/* Allocation of managed memory - that is, from the memory space that is
 * managed by the garbage collector. Memory that is in turn owned by a
 * GC-able object will tend to be allocated from fixed size pools. */

#include "moarvm.h"
 
/* Allocate the specified amount of memory from the nursery. Will
 * trigger a GC run if there is not enough. */
void * MVM_gc_allocate(MVMThreadContext *tc, size_t size) {
    void *allocated;
    
    /* Guard against 0-byte allocation. */
    if (size > 0) {
        /* Do a GC run if this allocation won't fit in what we have
         * left in the nursery. */
        if ((char *)tc->nursery_alloc + size >= tc->nursery_alloc_limit) {
            if (size > MVM_NURSERY_SIZE)
                MVM_panic("Attempt to allocate more than the maximum nursery size");
            /* XXX Call the GC. */
            MVM_panic("Out of memory; GC not yet implemented!");
        }
        
        /* Allocate (just bump the pointer). */
        allocated = tc->nursery_alloc;
        tc->nursery_alloc = (char *)tc->nursery_alloc + size;
    }
    else {
        MVM_panic("Cannot allocate 0 bytes of memory in the nursery");
    }
    
    return allocated;
}

/* Same as MVM_gc_allocate, but explicitly zeroes the memory that is
 * returned. */
void * MVM_gc_allocate_zeroed(MVMThreadContext *tc, size_t size) {
    void *allocated = MVM_gc_allocate(tc, size);
    memset(allocated, 0, size);
    return allocated;
}

/* Allocates a new STable, based on the specified thread context, REPR
 * and meta-object. */
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, MVMREPROps *repr, MVMObject *how) {
    MVMSTable *st    = MVM_gc_allocate_zeroed(tc, sizeof(MVMSTable));
    st->header.flags = MVM_CF_STABLE;
    st->header.owner = tc->thread_id;
    st->REPR         = repr;
    st->HOW          = how;
    return st;
}

/* Allocates a new type object. */
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj    = MVM_gc_allocate_zeroed(tc, sizeof(MVMObject));
    obj->header.flags = MVM_CF_TYPE_OBJECT;
    obj->header.owner = tc->thread_id;
    obj->st           = st;
    return obj;
}

/* Allocates a new object, and points it at the specified STable. */
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st, size_t size) {
    MVMObject *obj    = MVM_gc_allocate_zeroed(tc, size);
    obj->header.owner = tc->thread_id;
    obj->st           = st;
    return obj;
}
