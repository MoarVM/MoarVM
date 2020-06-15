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

/* Sets the passed thread context's thread up so that we'll run a finalize
 * handler on it in the near future. */
static void finalize_handler_caller(MVMThreadContext *tc, void *sr_data) {
    MVMObject *handler = MVM_hll_current(tc)->finalize_handler;
    if (handler) {
        MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
        MVMObject *drain;

        /* Drain the finalizing queue to an array. */
        MVMROOT(tc, handler, {
            drain = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
            while (tc->num_finalizing > 0)
                MVM_repr_push_o(tc, drain, tc->finalizing[--tc->num_finalizing]);
        });

        /* Invoke the handler. */
        handler = MVM_frame_find_invokee(tc, handler, NULL);
        MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, inv_arg_callsite);
        tc->cur_frame->args[0].o = drain;
        STABLE(handler)->invoke(tc, handler, inv_arg_callsite, tc->cur_frame->args);
    }
}
static void setup_finalize_handler_call(MVMThreadContext *tc) {
    MVMFrame *install_on = tc->cur_frame;
    while (install_on) {
        if (!install_on->extra || !install_on->extra->special_return)
            if (install_on->static_info->body.cu->body.hll_config)
                break;
        install_on = install_on->caller;
    }
    if (install_on)
        MVM_frame_special_return(tc, install_on, finalize_handler_caller, NULL,
            NULL, NULL);
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
    }
    tc->num_finalize = collapse_pos;
}
void MVM_finalize_walk_queues(MVMThreadContext *tc, MVMuint8 gen) {
    MVMThread *cur_thread = (MVMThread *)MVM_load(&tc->instance->threads);
    while (cur_thread) {
        if (cur_thread->body.tc) {
            walk_thread_finalize_queue(cur_thread->body.tc, gen);
            if (cur_thread->body.tc->num_finalizing > 0) {
                MVM_gc_collect(cur_thread->body.tc, MVMGCWhatToDo_Finalizing, gen);
                setup_finalize_handler_call(cur_thread->body.tc);
            }
        }
        cur_thread = cur_thread->body.next;
    }
}
