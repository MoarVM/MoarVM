#include "moar.h"

/* Writes to stderr about each OSR that we perform. */
#define MVM_LOG_OSR 0

/* Locates deopt index matching OSR point. */
static MVMint32 get_osr_deopt_index(MVMThreadContext *tc, MVMSpeshCandidate *cand) {
    /* Calculate offset. */
    MVMint32 offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start));

    /* Locate it in the deopt table. */
    MVMuint32 i;
    for (i = 0; i < cand->body.num_deopts; i++)
        if (cand->body.deopts[2 * i] == offset)
            return i;

    /* If we couldn't locate it, something is really very wrong. */
    MVM_oops(tc, "Spesh: get_osr_deopt_index failed");
}

/* Does the jump into the optimized code. */
void perform_osr(MVMThreadContext *tc, MVMSpeshCandidate *specialized) {
    MVMJitCode *jit_code;
    MVMint32 num_locals;
    /* Work out the OSR deopt index, to locate the entry point. */
    MVMint32 osr_index = get_osr_deopt_index(tc, specialized);
#if MVM_LOG_OSR
    fprintf(stderr, "Performing OSR of frame '%s' (cuid: %s) at index %d\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid),
        osr_index);
#endif

    jit_code = specialized->body.jitcode;
    num_locals = jit_code && jit_code->local_types ?
        jit_code->num_locals : specialized->body.num_locals;

    /* Resize work area if needed. */
    if (specialized->body.work_size > tc->cur_frame->allocd_work) {
        /* Resize work area. */
        MVMRegister *new_work = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            specialized->body.work_size);
        MVMRegister *new_args = new_work + num_locals;
        memcpy(new_work, tc->cur_frame->work,
            tc->cur_frame->static_info->body.num_locals * sizeof(MVMRegister));
        memcpy(new_args, tc->cur_frame->args,
            tc->cur_frame->static_info->body.cu->body.max_callsite_size * sizeof(MVMRegister));

        MVM_fixed_size_free(tc, tc->instance->fsa, tc->cur_frame->allocd_work,
            tc->cur_frame->work);
        tc->cur_frame->work = new_work;
        tc->cur_frame->allocd_work = specialized->body.work_size;
        tc->cur_frame->args = new_args;
#if MVM_LOG_OSR
    fprintf(stderr, "OSR resized work area of frame '%s' (cuid: %s)\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    }
    else if (specialized->body.work_size > tc->cur_frame->static_info->body.work_size) {
        size_t keep_bytes = tc->cur_frame->static_info->body.num_locals * sizeof(MVMRegister);
        size_t to_null = specialized->body.work_size - keep_bytes;
        memset((char *)tc->cur_frame->work + keep_bytes, 0, to_null);
    }

    /* Resize environment if needed. */
    if (specialized->body.num_lexicals > tc->cur_frame->static_info->body.num_lexicals) {
        MVMRegister *new_env = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
            specialized->body.env_size);
        if (tc->cur_frame->allocd_env) {
            memcpy(new_env, tc->cur_frame->env,
                tc->cur_frame->static_info->body.num_lexicals * sizeof(MVMRegister));
            MVM_fixed_size_free(tc, tc->instance->fsa, tc->cur_frame->allocd_env,
                tc->cur_frame->env);
        }
        tc->cur_frame->env = new_env;
        tc->cur_frame->allocd_env = specialized->body.env_size;
#if MVM_LOG_OSR
    fprintf(stderr, "OSR resized environment of frame '%s' (cuid: %s)\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    }
    else if (specialized->body.env_size > tc->cur_frame->static_info->body.env_size) {
        size_t keep_bytes = tc->cur_frame->static_info->body.num_lexicals * sizeof(MVMRegister);
        size_t to_null = specialized->body.env_size - keep_bytes;
        memset((char *)tc->cur_frame->env + keep_bytes, 0, to_null);
    }

    /* Set up frame to point to spesh candidate/slots. */
    tc->cur_frame->effective_spesh_slots = specialized->body.spesh_slots;
    MVM_ASSIGN_REF(tc, &(tc->cur_frame->header), tc->cur_frame->spesh_cand, specialized);

    /* Move into the optimized (and maybe JIT-compiled) code. */

    if (jit_code && jit_code->num_deopts) {
        MVMuint32 i;
        *(tc->interp_bytecode_start)   = jit_code->bytecode;
        *(tc->interp_cur_op)           = jit_code->bytecode;
        for (i = 0; i < jit_code->num_deopts; i++) {
            if (jit_code->deopts[i].idx == osr_index) {
                tc->cur_frame->jit_entry_label = jit_code->labels[jit_code->deopts[i].label];
                break;
            }
        }

        if (i == jit_code->num_deopts)
            MVM_oops(tc, "JIT: Could not find OSR label");
        if (tc->instance->profiling)
            MVM_profiler_log_osr(tc, 1);
    } else {
        *(tc->interp_bytecode_start) = specialized->body.bytecode;
        *(tc->interp_cur_op)         = specialized->body.bytecode +
            specialized->body.deopts[2 * osr_index + 1];
        if (tc->instance->profiling)
            MVM_profiler_log_osr(tc, 0);
    }
    *(tc->interp_reg_base) = tc->cur_frame->work;
}

/* Finds the appropriate callsite and args to use when running an arg guard for
 * OSR. */
MVMCallsite * find_callsite_and_args(MVMThreadContext *tc, MVMRegister **args) {
    MVMFrame *caller = tc->cur_frame->caller;
    if (caller) {
        if (caller->cur_args_callsite) {
            /* Normal call; take the current args buffer too. */
            *args = caller->args;
            return caller->cur_args_callsite;
        }
        else {
            /* Probably an invoke with capture. */
            if (caller->extra && caller->extra->invoked_call_capture) {
                /* Ensure what we have is compatible with what was invoked. */
                MVMCallCapture *cc = (MVMCallCapture *)caller->extra->invoked_call_capture;
                if (cc->body.apc->legacy.callsite == tc->cur_frame->params.legacy.callsite &&
                        cc->body.apc->legacy.args == tc->cur_frame->params.legacy.args) {
                    *args = cc->body.apc->legacy.args;
                    return cc->body.apc->legacy.callsite;
                }
            }

            /* Otherwise, no idea what. */
            *args = NULL;
            return NULL;
        }
    }
    else {
        /* No call, so it's the entry point frame. */
        *args = NULL;
        return MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS);
    }
}

/* Polls for an optimization and, when one is produced, jumps into it. */
void MVM_spesh_osr_poll_for_result(MVMThreadContext *tc) {
    MVMStaticFrameSpesh *spesh = tc->cur_frame->static_info->body.spesh;
    MVMint32 num_cands = spesh->body.num_spesh_candidates;
    MVMint32 seq_nr = tc->cur_frame->sequence_nr;
    if (seq_nr != tc->osr_hunt_frame_nr || num_cands != tc->osr_hunt_num_spesh_candidates) {
        /* Provided OSR is enabled... */
        if (tc->instance->spesh_osr_enabled) {
            /* Check if there's a candidate available and install it if so. */
            MVMRegister *args;
            MVMCallsite *cs = find_callsite_and_args(tc, &args);
            MVMint32 ag_result = MVM_spesh_arg_guard_run(tc,
                spesh->body.spesh_arg_guard,
                (cs && cs->is_interned ? cs : NULL),
                args, NULL);
            if (ag_result >= 0)
                perform_osr(tc, spesh->body.spesh_candidates[ag_result]);
        }

        /* Update state for avoiding checks in the common case. */
        tc->osr_hunt_frame_nr = seq_nr;
        tc->osr_hunt_num_spesh_candidates = num_cands;
    }
}
