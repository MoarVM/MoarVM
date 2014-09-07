#include "moar.h"

/* Turns finalization on or off for a type. */
void MVM_gc_finalize_set(MVMThreadContext *tc, MVMObject *type, MVMint64 finalize) {
    MVMSTable *st        = STABLE(type);
    MVMint64   new_flags = st->mode_flags & (~MVM_FINALIZE_TYPE);
    if (finalize)
        new_flags |= MVM_FINALIZE_TYPE;
    st->mode_flags = new_flags;
    MVM_SC_WB_ST(tc, st);
}

/* Adds an object we've just allocated to the queue of those with finalizers
 * that will need calling upon collection. */
void MVM_gc_finalize_add_to_queue(MVMThreadContext *tc, MVMObject *obj) {
    if (tc->num_finalize == tc->alloc_finalize) {
        if (tc->alloc_finalize)
            tc->alloc_finalize *= 2;
        else
            tc->alloc_finalize = 64;
        tc->finalize = MVM_realloc(tc->finalize,
            sizeof(MVMCollectable **) * tc->alloc_finalize);
    }
    tc->finalize[tc->num_finalize] = obj;
    tc->num_finalize++;
}
