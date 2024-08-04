/* Allocation of managed memory - that is, from the memory space that is
 * managed by the garbage collector. Memory that is in turn owned by a
 * GC-able object will be allocated separately and freed explicitly by
 * its REPR gc_free routine. */

#include "moar.h"

/* Allocate the specified amount of memory from the nursery. Will
 * trigger a GC run if there is not enough. */
void * MVM_gc_allocate_nursery(MVMThreadContext *tc, size_t size) {
    void *allocated;
    /* objects in the nursery must be aligned, so we pad at the end. The
     * assumption that they are aligned is also encoded in the collector */
    size = MVM_ALIGN_SIZE(size);

    /* Before an allocation is a GC safe-point and thus a good GC sync point
     * also; check if we've been signalled to collect. */
    /* Don't use a MVM_load(&tc->gc_status) here for performance, it's okay
     * if the interrupt is delayed a bit. */
    if (MVM_UNLIKELY(tc->gc_status))
        MVM_gc_enter_from_interrupt(tc);

    /* Guard against 0-byte allocation. */
    if (MVM_LIKELY(size > 0)) {
        /* Do a GC run if this allocation won't fit in what we have
         * left in the nursery. Note this is a loop to handle a
         * pathological case: all the objects in the nursery are very
         * young and thus survive in the nursery, meaning that no space
         * actually gets freed up. The next run will promote them to the
         * second generation. Note that this circumstance is exceptionally
         * unlikely in any non-contrived situation. */
#if MVM_GC_DEBUG < 3
        while (MVM_UNLIKELY((char *)tc->nursery_alloc + size >= (char *)tc->nursery_alloc_limit)) {
#endif
            if (size > MVM_NURSERY_SIZE)
                MVM_panic(MVM_exitcode_gcalloc, "Attempt to allocate more than the maximum nursery size");
            MVM_gc_enter_from_allocator(tc);
#if MVM_GC_DEBUG < 3
        }
#endif

        /* Allocate (just bump the pointer). */
        allocated = tc->nursery_alloc;
        tc->nursery_alloc = (char *)tc->nursery_alloc + size;
    }
    else {
        MVM_panic(MVM_exitcode_gcalloc, "Cannot allocate 0 bytes of memory in the nursery");
    }

    return allocated;
}

/* Allocates a new STable, based on the specified thread context, REPR
 * and meta-object. */
MVMSTable * MVM_gc_allocate_stable(MVMThreadContext *tc, const MVMREPROps *repr, MVMObject *how) {
    MVMSTable *st;
    MVMROOT(tc, how) {
        st                = MVM_gc_allocate_zeroed(tc, sizeof(MVMSTable));
        st->header.flags1 = MVM_CF_STABLE;
        st->header.size   = sizeof(MVMSTable);
        st->header.owner  = tc->thread_id;
        st->REPR          = repr;
        st->type_cache_id = MVM_6model_next_type_cache_id(tc);
        st->debug_name    = NULL;
        MVM_ASSIGN_REF(tc, &(st->header), st->HOW, how);
    }
    return st;
}

/* Allocates a new type object. */
MVMObject * MVM_gc_allocate_type_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVMROOT(tc, st) {
        obj                = MVM_gc_allocate_zeroed(tc, sizeof(MVMObject));
        obj->header.flags1 = MVM_CF_TYPE_OBJECT;
        obj->header.size   = sizeof(MVMObject);
        obj->header.owner  = tc->thread_id;
        MVM_ASSIGN_REF(tc, &(obj->header), obj->st, st);
    }
    return obj;
}

/* Allocates a new object, and points it at the specified STable. */
MVMObject * MVM_gc_allocate_object(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj;
    MVMROOT(tc, st) {
        obj               = MVM_gc_allocate_zeroed(tc, st->size);
        obj->header.size  = (MVMuint16)st->size;
        obj->header.owner = tc->thread_id;
        MVM_ASSIGN_REF(tc, &(obj->header), obj->st, st);
        if (st->mode_flags & MVM_FINALIZE_TYPE)
            MVM_gc_finalize_add_to_queue(tc, obj);
    }
    return obj;
}

/* Allocates a new heap frame. */
MVMFrame * MVM_gc_allocate_frame(MVMThreadContext *tc) {
    MVMFrame *f = MVM_gc_allocate_zeroed(tc, sizeof(MVMFrame));
    f->header.flags1 = MVM_CF_FRAME;
    f->header.size   = sizeof(MVMFrame);
    f->header.owner  = tc->thread_id;
    return f;
}
