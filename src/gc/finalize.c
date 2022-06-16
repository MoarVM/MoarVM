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
    MVM_ASSERT_NOT_FROMSPACE(tc, obj);
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
static void add_to_finalizing(MVMThreadContext *tc, MVMObject *obj) {
    if (tc->num_finalizing == tc->alloc_finalizing) {
        if (tc->alloc_finalizing)
            tc->alloc_finalizing *= 2;
        else
            tc->alloc_finalizing = 64;
        tc->finalizing = MVM_realloc(tc->finalizing,
            sizeof(MVMCollectable **) * tc->alloc_finalizing);
    }
    tc->finalizing[tc->num_finalizing] = obj;
    tc->num_finalizing++;
}
static void walk_thread_finalize_queue(MVMThreadContext *tc, MVMuint8 gen) {
    MVMuint32 collapse_pos = 0;
    MVMuint32 i;
    for (i = 0; i < tc->num_finalize; i++) {
        /* See if it's dead, taking which generation we've marked into
         * account. */
        MVMuint32 flags   = tc->finalize[i]->header.flags2;
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
                /* Dead; needs finalizing, so pop it on the finalizing list. */
                add_to_finalizing(tc, tc->finalize[i]);
            }
        }
        else {
            /* Just keep gen2 objects as they are during nursery-only collection */
            tc->finalize[collapse_pos++] = tc->finalize[i];
        }
    }
    tc->num_finalize = collapse_pos;
}
void MVM_finalize_walk_queues(MVMThreadContext *tc, MVMuint8 gen) {
    MVMThread *cur_thread = (MVMThread *)MVM_load(&tc->instance->threads);
    while (cur_thread) {
        if (cur_thread->body.tc) {
            walk_thread_finalize_queue(cur_thread->body.tc, gen);
            if (cur_thread->body.tc->num_finalizing > 0)
                MVM_gc_collect(cur_thread->body.tc, MVMGCWhatToDo_Finalizing, gen);
        }
        cur_thread = cur_thread->body.next;
    }
}

/* Try to run a finalization handler. Returns a true value if we do so */
void reinstate_last_handler_result(MVMThreadContext *tc, void *data) {
    tc->last_handler_result = *((MVMObject **)data);
}
void mark_last_handler_result(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    MVM_gc_worklist_add(tc, worklist, (MVMObject **)data);
}
void MVM_gc_finalize_run_handler(MVMThreadContext *tc) {
    /* Make sure there is a current frame and that there's a HLL handler
     * to run. */
    if (!tc->cur_frame)
        return;
    MVMCode *handler = MVM_hll_current(tc)->finalize_handler;
    if (handler) {
        /* Drain the finalizing queue to an array. */
        MVMObject *drain;
        MVMROOT(tc, handler, {
            drain = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            while (tc->num_finalizing > 0)
                MVM_repr_push_o(tc, drain, tc->finalizing[--tc->num_finalizing]);
        });

        /* If there is a last exception handler result stored, put something
         * in place to restore it, otherwise it may be overwritten during the
         * execution of finalizers. */
        if (tc->last_handler_result) {
            MVMObject **preserved = MVM_callstack_allocate_special_return(tc,
                reinstate_last_handler_result, reinstate_last_handler_result,
                mark_last_handler_result, sizeof(MVMObject *));
            *preserved = tc->last_handler_result;
        }

        /* Invoke the handler. */
        MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ));
        args_record->args.source[0].o = drain;
        MVM_frame_dispatch_from_c(tc, handler, args_record, NULL, MVM_RETURN_VOID);
    }
}
