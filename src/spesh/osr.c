#include "moar.h"

/* Locates deopt index matching OSR point. */
MVMint32 get_osr_deopt_index(MVMThreadContext *tc, MVMSpeshCandidate *cand) {
    /* Calculate offset. */
    MVMint32 offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start));

    /* Locate it in the deopt table. */
    MVMint32 i;
    for (i = 0; i < cand->num_deopts; i++)
        if (cand->deopts[2 * i] == offset)
            return i;

    /* If we couldn't locate it, something is really very wrong. */
    MVM_exception_throw_adhoc(tc, "Spesh: get_osr_deopt_index failed");
}

/* Called to start OSR. Switches us over to logging runs of spesh'd code, to
 * collect extra type info. */
void MVM_spesh_osr(MVMThreadContext *tc) {
    MVMSpeshCandidate *specialized;

    /* Ensure that we are in a position to specialize. */
    if (!tc->cur_frame->caller)
        return;
    if (!tc->cur_frame->params.callsite->is_interned)
        return;

    /* Produce logging spesh candidate. */
    specialized = MVM_spesh_candidate_setup(tc, tc->cur_frame->static_info,
        tc->cur_frame->params.callsite, tc->cur_frame->params.args, 1);
    if (specialized) {
        /* Set up frame to point to specialized logging code. */
        tc->cur_frame->effective_bytecode    = specialized->bytecode;
        tc->cur_frame->effective_handlers    = specialized->handlers;
        tc->cur_frame->effective_spesh_slots = specialized->spesh_slots;
        tc->cur_frame->spesh_log_slots       = specialized->log_slots;
        tc->cur_frame->spesh_cand            = specialized;
        tc->cur_frame->spesh_log_idx         = 0;

        /* Work out deopt index that applies, and move interpreter into the
         * logging version of the code. */
        specialized->osr_index       = get_osr_deopt_index(tc, specialized);
        *(tc->interp_bytecode_start) = specialized->bytecode;
        *(tc->interp_cur_op)         = specialized->bytecode +
                                       specialized->deopts[2 * specialized->osr_index + 1];
    }
}

/* Finalizes OSR. */
void MVM_spesh_osr_finalize(MVMThreadContext *tc) {
    /* Finish up the specialization. */
    MVMSpeshCandidate *specialized = tc->cur_frame->spesh_cand;
    MVM_spesh_candidate_specialize(tc, tc->cur_frame->static_info, specialized);

    /* XXX TODO: cope with inlining here. */
    if (specialized->num_inlines > 0)
        MVM_panic(1, "Spesh: OSR with inlining NYI");

    /* Sync frame with updates. */
    tc->cur_frame->effective_bytecode    = specialized->bytecode;
    tc->cur_frame->effective_handlers    = specialized->handlers;
    tc->cur_frame->effective_spesh_slots = specialized->spesh_slots;
    tc->cur_frame->spesh_log_slots       = specialized->log_slots;

    /* Sync interpreter with updates. */
    *(tc->interp_bytecode_start) = specialized->bytecode;
    *(tc->interp_cur_op)         = specialized->bytecode +
                                   specialized->deopts[2 * specialized->osr_index + 1];
}

