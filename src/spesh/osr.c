#include "moar.h"

/* Locates deopt index matching OSR point. */
static MVMint32 get_osr_deopt_index(MVMThreadContext *tc, MVMSpeshCandidate *cand) {
    /* Calculate offset. */
    MVMint32 offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start));

    /* Locate it in the deopt table. */
    MVMint32 i;
    for (i = 0; i < cand->num_deopts; i++)
        if (cand->deopts[2 * i] == offset)
            return i;

    /* If we couldn't locate it, something is really very wrong. */
    MVM_oops(tc, "Spesh: get_osr_deopt_index failed");
}

/* Does the jump into the optimized code. */
void perform_osr(MVMThreadContext *tc, MVMSpeshCandidate *specialized) {
    MVMJitCode *jc;
    MVMint32 osr_index;

    /* Resize work area if needed. */
    if (specialized->num_locals > tc->cur_frame->static_info->body.num_locals) {
        /* Resize work area. */
        MVMRegister *new_work = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            specialized->work_size);
        memcpy(new_work, tc->cur_frame->work,
            tc->cur_frame->static_info->body.num_locals * sizeof(MVMRegister));
        MVM_fixed_size_free(tc, tc->instance->fsa, tc->cur_frame->allocd_work,
            tc->cur_frame->work);
        tc->cur_frame->work = new_work;
        tc->cur_frame->allocd_work = specialized->work_size;
        tc->cur_frame->args = tc->cur_frame->work + specialized->num_locals;
    }

    /* Resize environment if needed. */
    if (specialized->num_lexicals > tc->cur_frame->static_info->body.num_lexicals) {
        MVMRegister *new_env = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            specialized->env_size);
        if (tc->cur_frame->allocd_env) {
            memcpy(new_env, tc->cur_frame->env,
                tc->cur_frame->static_info->body.num_lexicals * sizeof(MVMRegister));
            MVM_fixed_size_free(tc, tc->instance->fsa, tc->cur_frame->allocd_env,
                tc->cur_frame->env);
        }
        tc->cur_frame->env = new_env;
        tc->cur_frame->allocd_env = specialized->env_size;
    }

    /* Set up frame to point to specialized code. */
    tc->cur_frame->effective_bytecode    = specialized->bytecode;
    tc->cur_frame->effective_handlers    = specialized->handlers;
    tc->cur_frame->effective_spesh_slots = specialized->spesh_slots;
    tc->cur_frame->spesh_cand            = specialized;

    /* Work out deopt index that applies, and move interpreter the optimized
     * (and maybe JIT-compiled) code. */
    osr_index = get_osr_deopt_index(tc, specialized);
    jc = specialized->jitcode;
    if (jc && jc->num_deopts) {
        MVMint32 i;
        *(tc->interp_bytecode_start)   = specialized->jitcode->bytecode;
        *(tc->interp_cur_op)           = specialized->jitcode->bytecode;
        for (i = 0; i < jc->num_deopts; i++) {
            if (jc->deopts[i].idx == osr_index) {
                tc->cur_frame->jit_entry_label = jc->labels[jc->deopts[i].label];
                break;
            }
        }
        if (i == jc->num_deopts)
            MVM_oops(tc, "JIT: Could not find OSR label");
        if (tc->instance->profiling)
            MVM_profiler_log_osr(tc, 1);
    } else {
        *(tc->interp_bytecode_start) = specialized->bytecode;
        *(tc->interp_cur_op)         = specialized->bytecode +
            specialized->deopts[2 * osr_index + 1];
        if (tc->instance->profiling)
            MVM_profiler_log_osr(tc, 0);
    }
    *(tc->interp_reg_base) = tc->cur_frame->work;
}

/* Polls for an optimization and, when one is produced, jumps into it. */
void MVM_spesh_osr_poll_for_result(MVMThreadContext *tc) {
    MVMint32 seq_nr = tc->cur_frame->sequence_nr;
    MVMint32 num_cands = tc->cur_frame->static_info->body.num_spesh_candidates;
    if (seq_nr != tc->osr_hunt_frame_nr || num_cands != tc->osr_hunt_num_spesh_candidates) {
        /* Check if there's a candidate available and install it if so. */
        MVMCallsite *cs = tc->cur_frame->caller->cur_args_callsite;
        MVMint32 ag_result = MVM_spesh_arg_guard_run(tc,
            tc->cur_frame->static_info->body.spesh_arg_guard,
            (cs && cs->is_interned ? cs : NULL),
            tc->cur_frame->caller->args);
        if (ag_result >= 0)
            perform_osr(tc, tc->cur_frame->static_info->body.spesh_candidates[ag_result]);

        /* Update state for avoiding checks in the common case. */
        tc->osr_hunt_frame_nr = seq_nr;
        tc->osr_hunt_num_spesh_candidates = num_cands;
    }
}
