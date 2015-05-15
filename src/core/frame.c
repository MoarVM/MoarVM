#include "moar.h"

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
static void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    MVMStaticFrameBody *static_frame_body = &static_frame->body;

    /* Ensure the frame is fully deserialized. */
    if (!static_frame_body->fully_deserialized)
        MVM_bytecode_finish_frame(tc, static_frame_body->cu, static_frame, 0);

    /* Work size is number of locals/registers plus size of the maximum
     * call site argument list. */
    static_frame_body->work_size = sizeof(MVMRegister) *
        (static_frame_body->num_locals + static_frame_body->cu->body.max_callsite_size);

    /* Validate the bytecode. */
    MVM_validate_static_frame(tc, static_frame);

    /* Obtain an index to each threadcontext's lexotic pool table */
    static_frame_body->pool_index = MVM_incr(&tc->instance->num_frames_run);

    /* Check if we have any state var lexicals. */
    if (static_frame_body->static_env_flags) {
        MVMuint8 *flags  = static_frame_body->static_env_flags;
        MVMint64  numlex = static_frame_body->num_lexicals;
        MVMint64  i;
        for (i = 0; i < numlex; i++)
            if (flags[i] == 2) {
                static_frame_body->has_state_vars = 1;
                break;
            }
    }

    /* Set its spesh threshold. */
    static_frame_body->spesh_threshold = MVM_spesh_threshold(tc, static_frame);
}

/* When we don't match the current instrumentation level, we hit this. It may
 * simply be that we never invoked the frame, in which case we prepare and
 * verify it. It may also be because we need to instrument the code for
 * profiling. */
static void instrumentation_level_barrier(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    /* Prepare and verify if needed. */
    if (static_frame->body.instrumentation_level == 0)
        prepare_and_verify_static_frame(tc, static_frame);

    /* Mark frame as being at the current instrumentation level. */
    static_frame->body.instrumentation_level = tc->instance->instrumentation_level;

    /* Add profiling instrumentation if needed. */
    if (tc->instance->profiling)
        MVM_profile_instrument(tc, static_frame);
    else
        MVM_profile_ensure_uninstrumented(tc, static_frame);
}

/* Increases the reference count of a frame. */
MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame) {
    MVM_incr(&frame->ref_count);
    return frame;
}

/* Decreases the reference count of a frame. If it hits zero, then we can
 * free it. Returns null for convenience. */
MVMFrame * MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame) {
    /* MVM_dec returns what the count was before it decremented it
     * to zero, so we look for 1 here. */
    while (MVM_decr(&frame->ref_count) == 1) {
        MVMuint32 pool_index = frame->static_info->body.pool_index;
        MVMFrame *outer_to_decr = frame->outer;

        /* If there's a caller pointer, decrement that. */
        if (frame->caller)
            frame->caller = MVM_frame_dec_ref(tc, frame->caller);

        /* Destroy the frame. */
        if (frame->env) {
            MVM_fixed_size_free(tc, tc->instance->fsa, frame->allocd_env,
                frame->env);
        }
        if (frame->work) {
            MVM_args_proc_cleanup(tc, &frame->params);
            MVM_fixed_size_free(tc, tc->instance->fsa, frame->allocd_work,
                frame->work);
        }
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMFrame), frame);

        /* Decrement any outer. */
        if (outer_to_decr)
            frame = outer_to_decr; /* and loop */
        else
            break;
    }
    return NULL;
}

/* Creates a frame for usage as a context only, possibly forcing all of the
 * static lexicals to be deserialized if it's used for auto-close purposes. */
static MVMFrame * create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref, MVMint32 autoclose) {
    MVMFrame *frame;

    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (static_frame->body.instrumentation_level == 0)
        instrumentation_level_barrier(tc, static_frame);

    frame = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, sizeof(MVMFrame));

    /* Copy thread context into the frame. */
    frame->tc = tc;

    /* Set static frame. */
    frame->static_info = static_frame;

    /* Store the code ref. */
    frame->code_ref = code_ref;

    /* Allocate space for lexicals, copying the default lexical environment
     * into place and, if we're auto-closing, making sure anything we'd clone
     * is vivified to prevent the clone (which is what creates the correct
     * BEGIN/INIT semantics). */
    if (static_frame->body.env_size) {
        frame->env = MVM_fixed_size_alloc(tc, tc->instance->fsa, static_frame->body.env_size);
        frame->allocd_env = static_frame->body.env_size;
        if (autoclose) {
            MVMuint16 i;
            for (i = 0; i < static_frame->body.num_lexicals; i++) {
                if (!static_frame->body.static_env[i].o && static_frame->body.static_env_flags[i] == 1) {
                    MVMint32 scid, objid;
                    if (MVM_bytecode_find_static_lexical_scref(tc, static_frame->body.cu,
                            static_frame, i, &scid, &objid)) {
                        MVMSerializationContext *sc = MVM_sc_get_sc(tc, static_frame->body.cu, scid);
                        if (sc == NULL)
                            MVM_exception_throw_adhoc(tc,
                                "SC not yet resolved; lookup failed");
                        MVM_ASSIGN_REF(tc, &(static_frame->common.header),
                            static_frame->body.static_env[i].o,
                            MVM_sc_get_object(tc, sc, objid));
                    }
                }
            }
        }
        memcpy(frame->env, static_frame->body.static_env, static_frame->body.env_size);
    }

    /* Initial reference count is 0; leave referencing it to the caller (it
     * varies between deserialization, autoclose, etc.) */
    return frame;
}

/* Creates a frame that is suitable for deserializing a context into. Starts
 * with a ref count of 1 due to being held by an SC. */
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref) {
    return MVM_frame_inc_ref(tc,
        create_context_only(tc, static_frame, code_ref, 0));
}

/* Provides auto-close functionality, for the handful of cases where we have
 * not ever been in the outer frame of something we're invoking. In this case,
 * we fake up a frame based on the static lexical environment. */
static MVMFrame * autoclose(MVMThreadContext *tc, MVMStaticFrame *needed) {
    MVMFrame *result;

    /* First, see if we can find one on the call stack; return it if so. */
    MVMFrame *candidate = tc->cur_frame;
    while (candidate) {
        if (candidate->static_info->body.bytecode == needed->body.bytecode)
            return candidate;
        candidate = candidate->caller;
    }

    /* If not, fake up a frame See if it also needs an outer. */
    result = create_context_only(tc, needed, (MVMObject *)needed->body.static_code, 1);
    if (needed->body.outer) {
        /* See if the static code object has an outer. */
        MVMCode *outer_code = needed->body.outer->body.static_code;
        if (outer_code->body.outer &&
                outer_code->body.outer->static_info->body.bytecode == needed->body.bytecode) {
            /* Yes, just take it. */
            result->outer = MVM_frame_inc_ref(tc, outer_code->body.outer);
        }
        else {
            /* Otherwise, recursively auto-close. */
            result->outer = MVM_frame_inc_ref(tc, autoclose(tc, needed->body.outer));
        }
    }
    return result;
}

/* Obtains memory for a frame. */
static MVMFrame * allocate_frame(MVMThreadContext *tc, MVMStaticFrameBody *static_frame_body,
                                 MVMSpeshCandidate *spesh_cand) {
    MVMFrame *frame = NULL;
    MVMFrame *node;
    MVMint32  env_size, work_size;

    /* Allocate the frame. */
    frame = MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMFrame));
    frame->params.named_used = NULL;

    /* Ensure special return pointers, continuation tags, dynlex cache,
     * and return address are null. */
    frame->special_return    = NULL;
    frame->special_unwind    = NULL;
    frame->continuation_tags = NULL;
    frame->dynlex_cache_name = NULL;
    frame->return_address    = NULL;
    frame->jit_entry_label   = NULL;

    /* Allocate space for lexicals and work area, copying the default lexical
     * environment into place. */
    env_size = spesh_cand ? spesh_cand->env_size : static_frame_body->env_size;
    if (env_size) {
        frame->env = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, env_size);
        frame->allocd_env = env_size;
    }
    else {
        frame->env = NULL;
        frame->allocd_env = 0;
    }
    work_size = spesh_cand ? spesh_cand->work_size : static_frame_body->work_size;
    if (work_size) {
        frame->work = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, work_size);
        frame->allocd_work = work_size;
    }
    else {
        frame->work = NULL;
        frame->allocd_work = 0;
    }

    /* Calculate args buffer position and make sure current call site starts
     * empty. */
    frame->args = work_size
        ? frame->work + (spesh_cand ? spesh_cand->num_locals : static_frame_body->num_locals)
        : NULL;
    frame->cur_args_callsite = NULL;

    return frame;
}

/* This exists to reduce the amount of pointer-fiddling that has to be
 * done by the JIT */
void MVM_frame_invoke_code(MVMThreadContext *tc, MVMCode *code,
                           MVMCallsite *callsite, MVMint32 spesh_cand) {
    MVM_frame_invoke(tc, code->body.sf, callsite,  tc->cur_frame->args,
                     code->body.outer, (MVMObject*)code, spesh_cand);
}

/* Takes a static frame and a thread context. Invokes the static frame. */
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref, MVMint32 spesh_cand) {
    MVMFrame *frame;
    MVMuint32 found_spesh;
    MVMStaticFrameBody *static_frame_body = &static_frame->body;

    /* If the frame was never invoked before, or never before at the current
     * instrumentation level, we need to trigger the instrumentation level
     * barrier. */
    if (static_frame_body->instrumentation_level != tc->instance->instrumentation_level)
        instrumentation_level_barrier(tc, static_frame);

    /* See if any specializations apply. */
    found_spesh = 0;
    if (spesh_cand >= 0 && spesh_cand < static_frame_body->num_spesh_candidates) {
        MVMSpeshCandidate *chosen_cand = &static_frame_body->spesh_candidates[spesh_cand];
        if (!chosen_cand->sg) {
            frame = allocate_frame(tc, static_frame_body, chosen_cand);
            frame->effective_bytecode    = chosen_cand->bytecode;
            frame->effective_handlers    = chosen_cand->handlers;
            frame->effective_spesh_slots = chosen_cand->spesh_slots;
            frame->spesh_cand            = chosen_cand;
            frame->spesh_log_idx         = -1;
            found_spesh                  = 1;
        }
    }
    if (!found_spesh && ++static_frame_body->invocations >= static_frame_body->spesh_threshold && callsite->is_interned) {
        /* Look for specialized bytecode. */
        MVMint32 num_spesh = static_frame_body->num_spesh_candidates;
        MVMSpeshCandidate *chosen_cand = NULL;
        MVMint32 i, j;
        for (i = 0; i < num_spesh; i++) {
            MVMSpeshCandidate *cand = &static_frame_body->spesh_candidates[i];
            if (cand->cs == callsite) {
                MVMint32 match = 1;
                for (j = 0; j < cand->num_guards; j++) {
                    MVMint32   pos = cand->guards[j].slot;
                    MVMSTable *st  = (MVMSTable *)cand->guards[j].match;
                    MVMObject *arg = args[pos].o;
                    if (!arg) {
                        match = 0;
                        break;
                    }
                    switch (cand->guards[j].kind) {
                    case MVM_SPESH_GUARD_CONC:
                        if (!IS_CONCRETE(arg) || STABLE(arg) != st)
                            match = 0;
                        break;
                    case MVM_SPESH_GUARD_TYPE:
                        if (IS_CONCRETE(arg) || STABLE(arg) != st)
                            match = 0;
                        break;
                    case MVM_SPESH_GUARD_DC_CONC: {
                        MVMRegister dc;
                        STABLE(arg)->container_spec->fetch(tc, arg, &dc);
                        if (!dc.o || !IS_CONCRETE(dc.o) || STABLE(dc.o) != st)
                            match = 0;
                        break;
                    }
                    case MVM_SPESH_GUARD_DC_TYPE: {
                        MVMRegister dc;
                        STABLE(arg)->container_spec->fetch(tc, arg, &dc);
                        if (!dc.o || IS_CONCRETE(dc.o) || STABLE(dc.o) != st)
                            match = 0;
                        break;
                    }
                    }
                    if (!match)
                        break;
                }
                if (match) {
                    chosen_cand = cand;
                    break;
                }
            }
        }

        /* If we didn't find any, and we're below the limit, can set up a
         * specialization. */
        if (!chosen_cand && num_spesh < MVM_SPESH_LIMIT && tc->instance->spesh_enabled)
            chosen_cand = MVM_spesh_candidate_setup(tc, static_frame,
                callsite, args, 0);

        /* Now try to use specialized bytecode. We may need to compete to
         * be a logging run of it. */
        if (chosen_cand) {
            if (chosen_cand->sg) {
                /* In the logging phase. Try to get a logging index; ensure it
                 * is not being used as an OSR logging candidate elsewhere. */
                AO_t cur_idx = MVM_load(&(chosen_cand->log_enter_idx));
                if (!chosen_cand->osr_logging && cur_idx < MVM_SPESH_LOG_RUNS) {
                    if (MVM_cas(&(chosen_cand->log_enter_idx), cur_idx, cur_idx + 1) == cur_idx) {
                        /* We get to log. */
                        frame = allocate_frame(tc, static_frame_body, chosen_cand);
                        frame->effective_bytecode    = chosen_cand->bytecode;
                        frame->effective_handlers    = chosen_cand->handlers;
                        frame->effective_spesh_slots = chosen_cand->spesh_slots;
                        frame->spesh_log_slots       = chosen_cand->log_slots;
                        frame->spesh_cand            = chosen_cand;
                        frame->spesh_log_idx         = (MVMint8)cur_idx;
                        found_spesh                  = 1;
                    }
                }
            }
            else {
                /* In the post-specialize phase; can safely used the code. */
                frame = allocate_frame(tc, static_frame_body, chosen_cand);
                if (chosen_cand->jitcode) {
                    frame->effective_bytecode = chosen_cand->jitcode->bytecode;
                    frame->jit_entry_label    = chosen_cand->jitcode->labels[0];
                }
                else {
                    frame->effective_bytecode = chosen_cand->bytecode;
                }
                frame->effective_handlers    = chosen_cand->handlers;
                frame->effective_spesh_slots = chosen_cand->spesh_slots;
                frame->spesh_cand            = chosen_cand;
                frame->spesh_log_idx         = -1;
                found_spesh                  = 1;
            }
        }
    }
    if (!found_spesh) {
        frame = allocate_frame(tc, static_frame_body, NULL);
        frame->effective_bytecode = static_frame_body->bytecode;
        frame->effective_handlers = static_frame_body->handlers;
        frame->spesh_cand         = NULL;
    }

    /* Copy thread context (back?) into the frame. */
    frame->tc = tc;

    /* Set static frame. */
    frame->static_info = static_frame;

    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;

    /* Outer. */
    if (outer) {
        /* We were provided with an outer frame; just ensure that it is
         * based on the correct static frame (compare on bytecode address
         * to cope with nqp::freshcoderef). */
        if (outer->static_info->body.orig_bytecode == static_frame_body->outer->body.orig_bytecode)
            frame->outer = outer;
        else
            MVM_exception_throw_adhoc(tc,
                "When invoking %s '%s', provided outer frame %p (%s '%s') does not match expected static frame %p (%s '%s')",
                MVM_string_utf8_encode_C_string(tc, static_frame_body->cuuid),
                static_frame_body->name ? MVM_string_utf8_encode_C_string(tc, static_frame_body->name) : "<anonymous static frame>",
                outer->static_info,
                MVM_string_utf8_encode_C_string(tc, outer->static_info->body.cuuid),
                outer->static_info->body.name ? MVM_string_utf8_encode_C_string(tc, outer->static_info->body.name) : "<anonymous static frame>",
                static_frame_body->outer,
                MVM_string_utf8_encode_C_string(tc, static_frame_body->outer->body.cuuid),
                static_frame_body->outer->body.name ? MVM_string_utf8_encode_C_string(tc, static_frame_body->outer->body.name) : "<anonymous static frame>");
    }
    else if (static_frame_body->static_code && static_frame_body->static_code->body.outer) {
        /* We're lacking an outer, but our static code object may have one.
         * This comes up in the case of cloned protoregexes, for example. */
        frame->outer = static_frame_body->static_code->body.outer;
    }
    else if (static_frame_body->outer) {
        /* Auto-close, and cache it in the static frame. */
        frame->outer = autoclose(tc, static_frame_body->outer);
        static_frame_body->static_code->body.outer = MVM_frame_inc_ref(tc, frame->outer);
    }
    else {
        frame->outer = NULL;
    }
    if (frame->outer)
        MVM_frame_inc_ref(tc, frame->outer);

    /* Caller is current frame in the thread context. */
    if (tc->cur_frame)
        frame->caller = MVM_frame_inc_ref(tc, tc->cur_frame);
    else
        frame->caller = NULL;
    frame->keep_caller = 0;
    frame->in_continuation = 0;

    /* Initial reference count is 1 by virtue of it being the currently
     * executing frame. */
    frame->ref_count = 1;
    frame->gc_seq_number = 0;

    /* Initialize argument processing. */
    MVM_args_proc_init(tc, &frame->params, callsite, args);

    /* Make sure there's no frame context pointer and special return data. */
    frame->context_object = NULL;
    frame->special_return_data = NULL;
    frame->mark_special_return_data = NULL;

    /* Clear frame flags. */
    frame->flags = 0;

    /* Initialize OSR counter. */
    frame->osr_counter = 0;

    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    *(tc->interp_cur_op) = frame->effective_bytecode;
    *(tc->interp_bytecode_start) = frame->effective_bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame_body->cu;

    /* If we need to do so, make clones of things in the lexical environment
     * that need it. Note that we do this after tc->cur_frame became the
     * current frame, to make sure these new objects will certainly get
     * marked if GC is triggered along the way. */
    if (static_frame_body->has_state_vars) {
        /* Drag everything out of static_frame_body before we start,
         * as GC action may invalidate it. */
        MVMRegister *env       = static_frame_body->static_env;
        MVMuint8    *flags     = static_frame_body->static_env_flags;
        MVMint64     numlex    = static_frame_body->num_lexicals;
        MVMRegister *state     = NULL;
        MVMint64     state_act = 0; /* 0 = none so far, 1 = first time, 2 = later */
        MVMint64 i;
        for (i = 0; i < numlex; i++) {
            if (flags[i] == 2) {
                redo_state:
                switch (state_act) {
                case 0:
                    if (!frame->code_ref)
                        MVM_exception_throw_adhoc(tc,
                            "Frame must have code-ref to have state variables");
                    state = ((MVMCode *)frame->code_ref)->body.state_vars;
                    if (state) {
                        /* Already have state vars; pull them from this. */
                        state_act = 2;
                    }
                    else {
                        /* Allocate storage for state vars. */
                        state = MVM_malloc(frame->static_info->body.env_size);
                        memset(state, 0, frame->static_info->body.env_size);
                        ((MVMCode *)frame->code_ref)->body.state_vars = state;
                        state_act = 1;

                        /* Note that this frame should run state init code. */
                        frame->flags |= MVM_FRAME_FLAG_STATE_INIT;
                    }
                    goto redo_state;
                case 1:
                    frame->env[i].o = MVM_repr_clone(tc, env[i].o);
                    MVM_ASSIGN_REF(tc, &(frame->code_ref->header), state[i].o, frame->env[i].o);
                    break;
                case 2:
                    frame->env[i].o = state[i].o;
                    break;
                }
            }
        }
    }
}

/* Creates a frame for de-optimization purposes. */
MVMFrame * MVM_frame_create_for_deopt(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                                      MVMCode *code_ref) {
    MVMFrame *frame = allocate_frame(tc, &(static_frame->body), NULL);
    frame->effective_bytecode       = static_frame->body.bytecode;
    frame->effective_handlers       = static_frame->body.handlers;
    frame->spesh_cand               = NULL;
    frame->tc                       = tc;
    frame->static_info              = static_frame;
    frame->code_ref                 = (MVMObject *)code_ref;
    frame->caller                   = NULL; /* Set up by deopt-er. */
    frame->keep_caller              = 0;
    frame->in_continuation          = 0;
    frame->ref_count                = 1; /* It'll be on the "stack". */
    frame->gc_seq_number            = 0;
    frame->context_object           = NULL;
    frame->special_return_data      = NULL;
    frame->mark_special_return_data = NULL;
    frame->flags                    = 0;
    frame->params.callsite          = NULL; /* We only ever deopt after args handling. */
    frame->params.arg_flags         = NULL;
    frame->params.named_used        = NULL;
    if (code_ref->body.outer)
        frame->outer = MVM_frame_inc_ref(tc, code_ref->body.outer);
    else
        frame->outer = NULL;
    return frame;
}

/* Removes a single frame, as part of a return or unwind. Done after any exit
 * handler has already been run. */
static MVMuint64 remove_one_frame(MVMThreadContext *tc, MVMuint8 unwind) {
    MVMFrame *returner = tc->cur_frame;
    MVMFrame *caller   = returner->caller;

    /* See if we were in a logging spesh frame, and need to complete the
     * specialization. */
    if (returner->spesh_cand && returner->spesh_log_idx >= 0) {
        if (returner->spesh_cand->osr_logging) {
            /* Didn't achieve enough log entries to complete the OSR, but
             * clearly hot, so specialize anyway. This also avoids races
             * when the candidate is called again later and still has
             * sp_osrfinalize instructions in it. */
            returner->spesh_cand->osr_logging = 0;
            MVM_spesh_candidate_specialize(tc, returner->static_info,
                returner->spesh_cand);
        }
        else if (MVM_decr(&(returner->spesh_cand->log_exits_remaining)) == 1) {
            MVM_spesh_candidate_specialize(tc, returner->static_info,
                returner->spesh_cand);
        }
    }

    /* Some cleanup we only need do if we're not a frame involved in a
     * continuation (otherwise we need to allow for multi-shot
     * re-invocation). */
    if (!returner->in_continuation) {
        /* Arguments buffer no longer in use (saves GC visiting it). */
        returner->cur_args_callsite = NULL;

        /* Clear up argument processing leftovers, if any. */
        if (returner->work) {
            MVM_args_proc_cleanup_for_cache(tc, &returner->params);
        }

        /* Clear up any continuation tags. */
        if (returner->continuation_tags) {
            MVMContinuationTag *tag = returner->continuation_tags;
            while (tag) {
                MVMContinuationTag *next = tag->next;
                MVM_free(tag);
                tag = next;
            }
            returner->continuation_tags = NULL;
        }

        /* Signal to the GC to ignore ->work */
        returner->tc = NULL;

        /* Unless we need to keep the caller chain in place, clear it up. */
        if (caller) {
            if (!returner->keep_caller) {
                MVM_frame_dec_ref(tc, caller);
                returner->caller = NULL;
            }
            else if (unwind) {
                caller->keep_caller = 1;
            }
        }
    }

    /* Decrement the frame's ref-count by the 1 it got by virtue of being the
     * currently executing frame. */
    MVM_frame_dec_ref(tc, returner);

    /* Switch back to the caller frame if there is one. */
    if (caller && returner != tc->thread_entry_frame) {
        tc->cur_frame = caller;
        *(tc->interp_cur_op) = caller->return_address;
        *(tc->interp_bytecode_start) = caller->effective_bytecode;
        *(tc->interp_reg_base) = caller->work;
        *(tc->interp_cu) = caller->static_info->body.cu;

        /* Handle any special return hooks. */
        if (caller->special_return || caller->special_unwind) {
            MVMSpecialReturn  sr  = caller->special_return;
            MVMSpecialReturn  su  = caller->special_unwind;
            void             *srd = caller->special_return_data;
            caller->special_return           = NULL;
            caller->special_unwind           = NULL;
            caller->special_return_data      = NULL;
            caller->mark_special_return_data = NULL;
            if (unwind && su)
                su(tc, srd);
            else if (!unwind && sr)
                sr(tc, srd);
        }

        return 1;
    }
    else {
        tc->cur_frame = NULL;
        return 0;
    }
}

/* Attempt to return from the current frame. Returns non-zero if we can,
 * and zero if there is nowhere to return to (which would signal the exit
 * of the interpreter). */
static void remove_after_handler(MVMThreadContext *tc, void *sr_data) {
    remove_one_frame(tc, 0);
}
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;

    if (cur_frame->static_info->body.has_exit_handler &&
            !(cur_frame->flags & MVM_FRAME_FLAG_EXIT_HAND_RUN)) {
        /* Set us up to run exit handler, and make it so we'll really exit the
         * frame when that has been done. */
        MVMFrame     *caller = cur_frame->caller;
        MVMHLLConfig *hll    = MVM_hll_current(tc);
        MVMObject    *handler;
        MVMObject    *result;
        MVMCallsite *two_args_callsite;

        if (!caller)
            MVM_exception_throw_adhoc(tc, "Entry point frame cannot have an exit handler");
        if (tc->cur_frame == tc->thread_entry_frame)
            MVM_exception_throw_adhoc(tc, "Thread entry point frame cannot have an exit handler");

        switch (caller->return_type) {
            case MVM_RETURN_OBJ:
                result = caller->return_value->o;
                break;
            case MVM_RETURN_INT:
                result = MVM_repr_box_int(tc, hll->int_box_type, caller->return_value->i64);
                break;
            case MVM_RETURN_NUM:
                result = MVM_repr_box_num(tc, hll->num_box_type, caller->return_value->n64);
                break;
            case MVM_RETURN_STR:
                result = MVM_repr_box_str(tc, hll->str_box_type, caller->return_value->s);
                break;
            default:
                result = NULL;
        }
        
        handler = MVM_frame_find_invokee(tc, hll->exit_handler, NULL);
        two_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ);
        MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, two_args_callsite);
        cur_frame->args[0].o = cur_frame->code_ref;
        cur_frame->args[1].o = result;
        cur_frame->special_return = remove_after_handler;
        cur_frame->flags |= MVM_FRAME_FLAG_EXIT_HAND_RUN;
        STABLE(handler)->invoke(tc, handler, two_args_callsite, cur_frame->args);
        return 1;
    }
    else {
        /* No exit handler, so a straight return. */
        return remove_one_frame(tc, 0);
    }
}

/* Unwinds execution state to the specified frame, placing control flow at either
 * an absolute or relative (to start of target frame) address and optionally
 * setting a returned result. */
typedef struct {
    MVMFrame  *frame;
    MVMuint8  *abs_addr;
    MVMuint32  rel_addr;
} MVMUnwindData;
static void continue_unwind(MVMThreadContext *tc, void *sr_data) {
    MVMUnwindData *ud  = (MVMUnwindData *)sr_data;
    MVMFrame *frame    = ud->frame;
    MVMuint8 *abs_addr = ud->abs_addr;
    MVMuint32 rel_addr = ud->rel_addr;
    MVM_free(sr_data);
    MVM_frame_unwind_to(tc, frame, abs_addr, rel_addr, NULL);
}
void MVM_frame_unwind_to(MVMThreadContext *tc, MVMFrame *frame, MVMuint8 *abs_addr,
                         MVMuint32 rel_addr, MVMObject *return_value) {
    while (tc->cur_frame != frame) {
        MVMFrame *cur_frame = tc->cur_frame;
        if (cur_frame->static_info->body.has_exit_handler &&
                !(cur_frame->flags & MVM_FRAME_FLAG_EXIT_HAND_RUN)) {
            /* We're unwinding a frame with an exit handler. Thus we need to
             * pause the unwind, run the exit handler, and keep enough info
             * around in order to finish up the unwind afterwards. */
            MVMFrame     *caller = cur_frame->caller;
            MVMHLLConfig *hll    = MVM_hll_current(tc);
            MVMObject    *handler;
            MVMCallsite *two_args_callsite;

            if (!caller)
                MVM_exception_throw_adhoc(tc, "Entry point frame cannot have an exit handler");
            if (cur_frame == tc->thread_entry_frame)
                MVM_exception_throw_adhoc(tc, "Thread entry point frame cannot have an exit handler");

            handler = MVM_frame_find_invokee(tc, hll->exit_handler, NULL);
            two_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ);
            MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, two_args_callsite);
            cur_frame->args[0].o = cur_frame->code_ref;
            cur_frame->args[1].o = NULL;
            cur_frame->special_return = continue_unwind;
            {
                MVMUnwindData *ud = MVM_malloc(sizeof(MVMUnwindData));
                ud->frame = frame;
                ud->abs_addr = abs_addr;
                ud->rel_addr = rel_addr;
                if (return_value)
                    MVM_exception_throw_adhoc(tc, "return_value + exit_handler case NYI");
                cur_frame->special_return_data = ud;
            }
            cur_frame->flags |= MVM_FRAME_FLAG_EXIT_HAND_RUN;
            STABLE(handler)->invoke(tc, handler, two_args_callsite, cur_frame->args);
            return;
        }
        else {
            /* If we're profiling, log an exit. */
            if (tc->instance->profiling)
                MVM_profile_log_unwind(tc);

            /* No exit handler, so just remove the frame. */
            if (!remove_one_frame(tc, 1))
                MVM_panic(1, "Internal error: Unwound entire stack and missed handler");
        }
    }
    if (abs_addr)
        *tc->interp_cur_op = abs_addr;
    else if (rel_addr)
        *tc->interp_cur_op = *tc->interp_bytecode_start + rel_addr;
    if (return_value)
        MVM_args_set_result_obj(tc, return_value, 1);
}

/* Gets a code object for a frame, lazily deserializing it if needed. */
MVMObject * MVM_frame_get_code_object(MVMThreadContext *tc, MVMCode *code) {
    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc, "getcodeobj needs a code ref");
    if (!code->body.code_object) {
        MVMStaticFrame *sf = code->body.sf;
        if (sf->body.code_obj_sc_dep_idx > 0) {
            MVMSerializationContext *sc = MVM_sc_get_sc(tc, sf->body.cu,
                sf->body.code_obj_sc_dep_idx - 1);
            if (sc == NULL)
                MVM_exception_throw_adhoc(tc,
                    "SC not yet resolved; lookup failed");
            MVM_ASSIGN_REF(tc, &(code->common.header), code->body.code_object,
                MVM_sc_get_object(tc, sc, sf->body.code_obj_sc_idx));
        }
    }
    return code->body.code_object;
}

/* Given the specified code object, sets its outer to the current scope. */
void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *code_obj = (MVMCode *)code;

    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform capturelex on object with representation MVMCode");

    /* Increment current frame reference. */
    MVM_frame_inc_ref(tc, tc->cur_frame);

    /* Try to replace outer; retry on failure (should hopefully be highly
     * rare). */
    do {
        MVMFrame *orig_outer = code_obj->body.outer;
        if (MVM_trycas(&(code_obj->body.outer), orig_outer, tc->cur_frame)) {
            /* Success; decrement any original outer and we're done. */
            if (orig_outer)
                MVM_frame_dec_ref(tc, orig_outer);
            return;
        }
    }
    while (1);
}

/* Given the specified code object, copies it and returns a copy which
 * captures a closure over the current scope. */
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *closure;

    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform takeclosure on object with representation MVMCode");

    MVMROOT(tc, code, {
        closure = (MVMCode *)REPR(code)->allocate(tc, STABLE(code));
    });

    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.sf, ((MVMCode *)code)->body.sf);
    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.name, ((MVMCode *)code)->body.name);
    closure->body.outer = MVM_frame_inc_ref(tc, tc->cur_frame);

    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.code_object,
        ((MVMCode *)code)->body.code_object);

    return (MVMObject *)closure;
}

/* Vivifies a lexical in a frame. */
MVMObject * MVM_frame_vivify_lexical(MVMThreadContext *tc, MVMFrame *f, MVMuint16 idx) {
    MVMuint8       *flags;
    MVMint16        flag;
    MVMRegister    *static_env;
    MVMuint16       effective_idx;
    MVMStaticFrame *effective_sf;
    if (idx < f->static_info->body.num_lexicals) {
        flags         = f->static_info->body.static_env_flags;
        static_env    = f->static_info->body.static_env;
        effective_idx = idx;
        effective_sf  = f->static_info;
    }
    else if (f->spesh_cand) {
        MVMint32 i;
        flags = NULL;
        for (i = 0; i < f->spesh_cand->num_inlines; i++) {
            MVMStaticFrame *isf = f->spesh_cand->inlines[i].code->body.sf;
            effective_idx = idx - f->spesh_cand->inlines[i].lexicals_start;
            if (effective_idx < isf->body.num_lexicals) {
                flags        = isf->body.static_env_flags;
                static_env   = isf->body.static_env;
                effective_sf = isf;
                break;
            }
        }
    }
    else {
        flags = NULL;
    }
    flag  = flags ? flags[effective_idx] : -1;
    if (flag != -1 && static_env[effective_idx].o == NULL) {
        MVMStaticFrameBody *static_frame_body = &(f->static_info->body);
        MVMint32 scid, objid;
        if (MVM_bytecode_find_static_lexical_scref(tc, effective_sf->body.cu,
                effective_sf, effective_idx, &scid, &objid)) {
            MVMSerializationContext *sc = MVM_sc_get_sc(tc, effective_sf->body.cu, scid);
            if (sc == NULL)
                MVM_exception_throw_adhoc(tc,
                    "SC not yet resolved; lookup failed");
            MVM_ASSIGN_REF(tc, &(f->static_info->common.header),
                static_frame_body->static_env[effective_idx].o,
                MVM_sc_get_object(tc, sc, objid));
        }
    }
    if (flag == 0) {
        MVMObject *viv = static_env[effective_idx].o;
        return f->env[idx].o = viv ? viv : tc->instance->VMNull;
    }
    else if (flag == 1) {
        MVMObject *viv = static_env[effective_idx].o;
        return f->env[idx].o = MVM_repr_clone(tc, viv);
    }
    else {
        return tc->instance->VMNull;
    }
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. Non-existing object lexicals produce NULL, expected
 * (for better or worse) by various things. Otherwise, an error is thrown
 * if it does not exist. Incorrect type always throws. */
MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
            MVMLexicalRegistry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == type) {
                    MVMRegister *result = &cur_frame->env[entry->value];
                    if (type == MVM_reg_obj && !result->o)
                        MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                    return result;
                }
                else {
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name '%s' has wrong type",
                            MVM_string_utf8_encode_C_string(tc, name));
                }
            }
        }
        cur_frame = cur_frame->outer;
    }
    if (type == MVM_reg_obj)
        return NULL;
    MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

/* Finds a lexical in the outer frame, throwing if it's not there. */
MVMObject * MVM_frame_find_lexical_by_name_outer(MVMThreadContext *tc, MVMString *name) {
    MVMRegister *r = MVM_frame_find_lexical_by_name_rel(tc, name, tc->cur_frame->outer);
    if (r)
        return r->o;
    else
        MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
            MVM_string_utf8_encode_C_string(tc, name));
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. Only works if it's an object lexical.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
            MVMLexicalRegistry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == MVM_reg_obj) {
                    MVMRegister *result = &cur_frame->env[entry->value];
                    if (!result->o)
                        MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                    return result;
                }
                else {
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name '%s' has wrong type",
                            MVM_string_utf8_encode_C_string(tc, name));
                }
            }
        }
        cur_frame = cur_frame->outer;
    }
    return NULL;
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. It checks all outer frames of the caller frame chain.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel_caller(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_caller_frame) {
    MVM_string_flatten(tc, name);
    while (cur_caller_frame != NULL) {
        MVMFrame *cur_frame = cur_caller_frame;
        while (cur_frame != NULL) {
            MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
            if (lexical_names) {
                /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
                MVMLexicalRegistry *entry;

                MVM_HASH_GET(tc, lexical_names, name, entry)

                if (entry) {
                    if (cur_frame->static_info->body.lexical_types[entry->value] == MVM_reg_obj) {
                        MVMRegister *result = &cur_frame->env[entry->value];
                        if (!result->o)
                            MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                        return result;
                    }
                    else {
                       MVM_exception_throw_adhoc(tc,
                            "Lexical with name '%s' has wrong type",
                                MVM_string_utf8_encode_C_string(tc, name));
                    }
                }
            }
            cur_frame = cur_frame->outer;
        }
        cur_caller_frame = cur_caller_frame->caller;
    }
    return NULL;
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. Returns null if it does not exist. */
static void try_cache_dynlex(MVMThreadContext *tc, MVMFrame *from, MVMFrame *to, MVMString *name, MVMRegister *reg, MVMuint16 type, MVMuint32 fcost, MVMuint32 icost) {
    MVMint32 next = 0;
    MVMint32 frames = 0;
    MVMuint32 desperation = 0;

    if (fcost+icost > 20)
        desperation = 1;

    while (from && from != to) {
        frames++;
        if (frames >= next) {
            if (!from->dynlex_cache_name || (desperation && frames > 1)) {
                from->dynlex_cache_name = name;
                from->dynlex_cache_reg  = reg;
                from->dynlex_cache_type = type;
                if (desperation && next == 3) {
                    next = fcost / 2;
                }
                else {
                    if (next)
                        return;
                    next = 3;
                }
            }
        }
        from = from->caller;
    }
}
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type, MVMFrame *cur_frame, MVMint32 vivify) {
    FILE *dlog = tc->instance->dynvar_log_fh;
    MVMuint32 fcost = 0;  /* frames traversed */
    MVMuint32 icost = 0;  /* inlines traversed */
    MVMuint32 ecost = 0;  /* frames traversed with empty cache */
    MVMuint32 xcost = 0;  /* frames traversed with wrong name */
    char *c_name;

    MVMFrame *initial_frame = cur_frame;
    if (!name)
        MVM_exception_throw_adhoc(tc, "Contextual name cannot be null");
    if (dlog)
        c_name = MVM_string_utf8_encode_C_string(tc, name);
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names;
        MVMSpeshCandidate  *cand     = cur_frame->spesh_cand;
        /* See if we are inside an inline. Note that this isn't actually
         * correct for a leaf frame, but those aren't inlined and don't
         * use getdynlex for their own lexicals since the compiler already
         * knows where to find them */
        if (cand && cand->num_inlines) {
            if (cand->jitcode) {
                void      **labels = cand->jitcode->labels;
                void *return_label = cur_frame->jit_entry_label;
                MVMJitInline *inls = cand->jitcode->inlines;
                MVMint32 i;
                for (i = 0; i < cand->jitcode->num_inlines; i++) {
                    icost++;
                    if (return_label >= labels[inls[i].start_label] && return_label <= labels[inls[i].end_label]) {
                        MVMStaticFrame *isf = cand->inlines[i].code->body.sf;
                        if ((lexical_names = isf->body.lexical_names)) {
                            MVMLexicalRegistry *entry;
                            MVM_HASH_GET(tc, lexical_names, name, entry);
                            if (entry) {
                                MVMuint16    lexidx = cand->inlines[i].lexicals_start + entry->value;
                                MVMRegister *result = &cur_frame->env[lexidx];
                                *type = cand->lexical_types[lexidx];
                                if (vivify && *type == MVM_reg_obj && !result->o)
                                    MVM_frame_vivify_lexical(tc, cur_frame, lexidx);
                                if (fcost+icost > 1)
                                  try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                                if (dlog) {
                                    fprintf(dlog, "I %s %d %d %d %d\n", c_name, fcost, icost, ecost, xcost);
                                    fflush(dlog);
                                    MVM_free(c_name);
                                }
                                return result;
                            }
                        }
                    }
                }
            } else {
                MVMint32 ret_offset = cur_frame->return_address - cur_frame->effective_bytecode;
                MVMint32 i;
                for (i = 0; i < cand->num_inlines; i++) {
                    icost++;
                    if (ret_offset >= cand->inlines[i].start && ret_offset < cand->inlines[i].end) {
                        MVMStaticFrame *isf = cand->inlines[i].code->body.sf;
                        if ((lexical_names = isf->body.lexical_names)) {
                            MVMLexicalRegistry *entry;
                            MVM_HASH_GET(tc, lexical_names, name, entry);
                            if (entry) {
                                MVMuint16    lexidx = cand->inlines[i].lexicals_start + entry->value;
                                MVMRegister *result = &cur_frame->env[lexidx];
                                *type = cand->lexical_types[lexidx];
                                if (vivify && *type == MVM_reg_obj && !result->o)
                                    MVM_frame_vivify_lexical(tc, cur_frame, lexidx);
                                if (fcost+icost > 1)
                                  try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                                if (dlog) {
                                    fprintf(dlog, "I %s %d %d %d %d\n", c_name, fcost, icost, ecost, xcost);
                                    fflush(dlog);
                                    MVM_free(c_name);
                                }
                                return result;
                            }

                        }

                    }
                }
            }
        }

        /* See if we've got it cached at this level. */
        if (cur_frame->dynlex_cache_name) {
            if (MVM_string_equal(tc, name, cur_frame->dynlex_cache_name)) {
                MVMRegister *result = cur_frame->dynlex_cache_reg;
                *type = cur_frame->dynlex_cache_type;
                if (fcost+icost > 5)
                    try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                if (dlog) {
                    fprintf(dlog, "C %s %d %d %d %d\n", c_name, fcost, icost, ecost, xcost);
                    fflush(dlog);
                    MVM_free(c_name);
                }
                return result;
            }
            else
                xcost++;
        }
        else
            ecost++;

        /* Now look in the frame itself. */
        if ((lexical_names = cur_frame->static_info->body.lexical_names)) {
            MVMLexicalRegistry *entry;
            MVM_HASH_GET(tc, lexical_names, name, entry)
            if (entry) {
                MVMRegister *result = &cur_frame->env[entry->value];
                *type = cur_frame->static_info->body.lexical_types[entry->value];
                if (vivify && *type == MVM_reg_obj && !result->o)
                    MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                if (dlog) {
                    fprintf(dlog, "F %s %d %d %d %d\n", c_name, fcost, icost, ecost, xcost);
                    fflush(dlog);
                    MVM_free(c_name);
                }
                if (fcost+icost > 1)
                    try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                return result;
            }
        }
        fcost++;
        cur_frame = cur_frame->caller;
    }
    if (dlog) {
        fprintf(dlog, "N %s %d %d %d %d\n", c_name, fcost, icost, ecost, xcost);
        fflush(dlog);
        MVM_free(c_name);
    }
    return NULL;
}

MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame, 1);
    MVMObject *result = NULL, *result_type = NULL;
    if (lex_reg) {
        switch (type) {
            case MVM_reg_int64:
                result_type = (*tc->interp_cu)->body.hll_config->int_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing int box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_int(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->i64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_num64:
                result_type = (*tc->interp_cu)->body.hll_config->num_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing num box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_num(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->n64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_str:
                result_type = (*tc->interp_cu)->body.hll_config->str_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing str box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_str(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->s);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_obj:
                result = lex_reg->o;
                break;
            default:
                MVM_exception_throw_adhoc(tc, "invalid register type in getdynlex");
        }
    }
    return result;
}

void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame, 0);
    if (!lex_reg) {
        MVM_exception_throw_adhoc(tc, "No contextual found with name '%s'",
            MVM_string_utf8_encode_C_string(tc, name));
    }
    switch (type) {
        case MVM_reg_int64:
            lex_reg->i64 = REPR(value)->box_funcs.get_int(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_num64:
            lex_reg->n64 = REPR(value)->box_funcs.get_num(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_str:
            lex_reg->s = REPR(value)->box_funcs.get_str(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_obj:
            lex_reg->o = value;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "invalid register type in binddynlex");
    }
}

/* Returns the storage unit for the lexical in the specified frame. Does not
 * try to vivify anything - gets exactly what is there. */
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry)
            return &f->env[entry->value];
    }
    MVM_exception_throw_adhoc(tc, "Frame has no lexical with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

/* Returns the storage unit for the lexical in the specified frame. */
MVMRegister * MVM_frame_try_get_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name, MVMuint16 type) {
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry && f->static_info->body.lexical_types[entry->value] == type) {
            MVMRegister *result = &f->env[entry->value];
            if (type == MVM_reg_obj && !result->o)
                MVM_frame_vivify_lexical(tc, f, entry->value);
            return result;
        }
    }
    return NULL;
}

/* Returns the primitive type specification for a lexical. */
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry) {
            switch (f->static_info->body.lexical_types[entry->value]) {
                case MVM_reg_int64:
                    return MVM_STORAGE_SPEC_BP_INT;
                case MVM_reg_num64:
                    return MVM_STORAGE_SPEC_BP_NUM;
                case MVM_reg_str:
                    return MVM_STORAGE_SPEC_BP_STR;
                case MVM_reg_obj:
                    return MVM_STORAGE_SPEC_BP_NONE;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "Unhandled lexical type in lexprimspec for '%s'",
                        MVM_string_utf8_encode_C_string(tc, name));
            }
        }
    }
    MVM_exception_throw_adhoc(tc, "Frame has no lexical with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

static MVMObject * find_invokee_internal(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs, MVMInvocationSpec *is) {
    if (!MVM_is_null(tc, is->class_handle)) {
        MVMRegister dest;
        REPR(code)->attr_funcs.get_attribute(tc,
            STABLE(code), code, OBJECT_BODY(code),
            is->class_handle, is->attr_name,
            is->hint, &dest, MVM_reg_obj);
        code = dest.o;
    }
    else {
        /* Need to tweak the callsite and args to include the code object
         * being invoked. */
        if (tweak_cs) {
            MVMCallsite *orig = *tweak_cs;
            if (orig->with_invocant) {
                *tweak_cs = orig->with_invocant;
            }
            else {
                MVMCallsite *new   = MVM_malloc(sizeof(MVMCallsite));
                MVMint32     fsize = orig->num_pos + (orig->arg_count - orig->num_pos) / 2;
                new->arg_flags     = MVM_malloc((fsize + 1) * sizeof(MVMCallsiteEntry));
                new->arg_flags[0]  = MVM_CALLSITE_ARG_OBJ;
                memcpy(new->arg_flags + 1, orig->arg_flags, fsize);
                new->arg_count      = orig->arg_count + 1;
                new->num_pos        = orig->num_pos + 1;
                new->has_flattening = orig->has_flattening;
                new->is_interned    = 0;
                new->with_invocant  = NULL;
                *tweak_cs = orig->with_invocant = new;
            }
            memmove(tc->cur_frame->args + 1, tc->cur_frame->args,
                orig->arg_count * sizeof(MVMRegister));
            tc->cur_frame->args[0].o = code;
            tc->cur_frame->cur_args_callsite = *tweak_cs; /* Keep in sync. */
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Cannot invoke object with invocation handler in this context");
        }
        code = is->invocation_handler;
    }
    return code;
}

MVMObject * MVM_frame_find_invokee(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs) {
    if (MVM_is_null(tc, code))
        MVM_exception_throw_adhoc(tc, "Cannot invoke null object");
    if (STABLE(code)->invoke == MVM_6model_invoke_default) {
        MVMInvocationSpec *is = STABLE(code)->invocation_spec;
        if (!is) {
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s)",
                REPR(code)->name);
        }
        code = find_invokee_internal(tc, code, tweak_cs, is);
    }
    return code;
}

MVM_USED_BY_JIT
MVMObject * MVM_frame_find_invokee_multi_ok(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs, MVMRegister *args) {
    if (!code)
        MVM_exception_throw_adhoc(tc, "Cannot invoke null object");
    if (STABLE(code)->invoke == MVM_6model_invoke_default) {
        MVMInvocationSpec *is = STABLE(code)->invocation_spec;
        if (!is) {
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s, cs = %d)",
                REPR(code)->name, STABLE(code)->container_spec ? 1 : 0);
        }
        if (!MVM_is_null(tc, is->md_class_handle)) {
            /* We might be able to dig straight into the multi cache and not
             * have to invoke the proto. */
            MVMRegister dest;
            REPR(code)->attr_funcs.get_attribute(tc,
                STABLE(code), code, OBJECT_BODY(code),
                is->md_class_handle, is->md_valid_attr_name,
                is->md_valid_hint, &dest, MVM_reg_int64);
            if (dest.i64) {
                REPR(code)->attr_funcs.get_attribute(tc,
                    STABLE(code), code, OBJECT_BODY(code),
                    is->md_class_handle, is->md_cache_attr_name,
                    is->md_cache_hint, &dest, MVM_reg_obj);
                if (!MVM_is_null(tc, dest.o)) {
                    MVMObject *result = MVM_multi_cache_find_callsite_args(tc,
                        dest.o, *tweak_cs, args);
                    if (result)
                        return MVM_frame_find_invokee(tc, result, tweak_cs);
                }
            }
        }
        code = find_invokee_internal(tc, code, tweak_cs, is);
    }
    return code;
}

MVMObject * MVM_frame_context_wrapper(MVMThreadContext *tc, MVMFrame *f) {
    MVMObject *ctx = (MVMObject *)MVM_load(&f->context_object);

    if (!ctx) {
        ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
        ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, f);

        if (MVM_casptr(&f->context_object, NULL, ctx) != NULL) {
            ((MVMContext *)ctx)->body.context = MVM_frame_dec_ref(tc, f);
            ctx = (MVMObject *)MVM_load(&f->context_object);
        }
    }

    return ctx;
}

/* Creates a shallow clone of a frame. Used by continuations. We rely on this
 * not allocating with the GC; update continuation clone code if it comes to
  * do so. */
MVMFrame * MVM_frame_clone(MVMThreadContext *tc, MVMFrame *f) {
    /* First, just grab a copy of everything. */
    MVMFrame *clone =  MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMFrame));
    memcpy(clone, f, sizeof(MVMFrame));

    /* Need fresh env and work. */
    if (f->static_info->body.env_size) {
        clone->env = MVM_fixed_size_alloc(tc, tc->instance->fsa, f->static_info->body.env_size);
        clone->allocd_env = f->static_info->body.env_size;
        memcpy(clone->env, f->env, f->static_info->body.env_size);
    }
    if (f->static_info->body.work_size) {
        clone->work = MVM_malloc(f->static_info->body.work_size);
        memcpy(clone->work, f->work, f->static_info->body.work_size);
        clone->args = clone->work + f->static_info->body.num_locals;
    }

    /* Ref-count of the clone is 1. */
    clone->ref_count = 1;

    /* If there's an outer, there's now an extra frame pointing at it. */
    if (clone->outer)
        MVM_frame_inc_ref(tc, clone->outer);

    return clone;
}
