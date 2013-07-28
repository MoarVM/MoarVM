/* Allocation of managed memory - that is, from the memory space that is
 * managed by the garbage collector. Memory that is in turn owned by a
 * GC-able object will be allocated separately and freed explicitly by
 * its REPR gc_free routine. */

#include "moarvm.h"

/* Allocate the specified amount of memory from the nursery. Will
 * trigger a GC run if there is not enough. */
void * MVM_gc_allocate_nursery(MVMThreadContext *tc, size_t size) {
    void *allocated;

    /* Before an allocation is a GC safe-point and thus a good GC sync point
     * also; check if we've been signalled to collect. */
    if (tc->gc_status)
        MVM_gc_enter_from_interrupt(tc);

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
            MVM_gc_enter_from_allocator(tc);
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
    MVMROOT(tc, how, {
        st                = MVM_gc_allocate_zeroed(tc, sizeof(MVMSTable));
        st->header.flags |= MVM_CF_STABLE;
        st->header.owner  = tc->thread_id;
        st->REPR          = repr;
        st->invoke        = MVM_6model_invoke_default;
        MVM_ASSIGN_REF(tc, st, st->HOW, how);
    });
    return st;
}

/* Allocates a new type object. */
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVMROOT(tc, st, {
        obj                = MVM_gc_allocate_zeroed(tc, sizeof(MVMObject));
        obj->header.flags |= MVM_CF_TYPE_OBJECT;
        obj->header.owner  = tc->thread_id;
        MVM_ASSIGN_REF(tc, obj, obj->st, st);
    });
    return obj;
}

/* Allocates a new object, and points it at the specified STable. */
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVMROOT(tc, st, {
        obj               = MVM_gc_allocate_zeroed(tc, st->size);
        obj->header.owner = tc->thread_id;
        MVM_ASSIGN_REF(tc, obj, obj->st, st);
        if (obj->header.flags & MVM_CF_SECOND_GEN)
            if (REPR(obj)->refs_frames)
                MVM_gc_root_gen2_add(tc, (MVMCollectable *)obj);
    });
    return obj;
}

/* Sets allocate for this thread to be from the second generation by
 * default. */
void MVM_gc_allocate_gen2_default_set(MVMThreadContext *tc) {
    tc->allocate_in = MVMAllocate_Gen2;
}

/* Sets allocation for this thread to be from the nursery by default. */
void MVM_gc_allocate_gen2_default_clear(MVMThreadContext *tc) {
    tc->allocate_in = MVMAllocate_Nursery;
}
