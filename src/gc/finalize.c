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
