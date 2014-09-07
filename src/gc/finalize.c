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

/* Walks through the per-thread finalize queues, identifying objects that
 * should be finalized, pushing them onto a finalize list, and then marking
 * that list entry. Assumes the world is stopped. */
void walk_thread_finalize_queue(MVMThreadContext *tc, MVMuint8 gen) {
    MVMuint32 collapse_pos = 0;
    MVMuint32 i;
    for (i = 0; i < tc->num_finalize; i++) {
        /* See if it's dead, taking which generation we've marked into
         * account. */
        MVMuint32 flags   = tc->finalize[i]->header.flags;
        MVMuint32 in_gen2 = flags & MVM_CF_SECOND_GEN;
        if (gen == MVMGCGenerations_Both || !in_gen2) {
            MVMuint32 live = flags & (MVM_CF_GEN2_LIVE | MVM_CF_FORWARDER_VALID);
            if (live) {
                /* Alive, so just leave it in finalized queue, taking updated
                 * address if needed. */
                tc->finalize[collapse_pos++] = flags & MVM_CF_FORWARDER_VALID
                    ? (MVMObject *)tc->finalize[i]->header.sc_forward_u.forwarder
                    : tc->finalize[i];
            }
            else {
                /* Dead; needs finalizing. */
                /* TODO */
            }
        }
    }
    tc->num_finalize = collapse_pos;
}
void MVM_finalize_walk_queues(MVMThreadContext *tc, MVMuint8 gen) {
    MVMThread *cur_thread = (MVMThread *)MVM_load(&tc->instance->threads);
    while (cur_thread) {
        if (cur_thread->body.tc)
            walk_thread_finalize_queue(cur_thread->body.tc, gen);
        cur_thread = cur_thread->body.next;
    }
}
