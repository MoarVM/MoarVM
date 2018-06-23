#include "moar.h"

/* This allows the dynlex cache to be disabled when bug hunting, if needed. */
#define MVM_DYNLEX_CACHE_ENABLED 1

/* Check spesh candidate pre-selections match the guards. */
#define MVM_SPESH_CHECK_PRESELECTION 0

/* Computes the initial work area for a frame or a specialization of a frame. */
MVMRegister * MVM_frame_initial_work(MVMThreadContext *tc, MVMuint16 *local_types,
                                     MVMuint16 num_locals) {
    MVMuint16 i;
    MVMRegister *work_initial = MVM_calloc(num_locals, sizeof(MVMRegister));
    for (i = 0; i < num_locals; i++)
        if (local_types[i] == MVM_reg_obj)
            work_initial[i].o = tc->instance->VMNull;
    return work_initial;
}

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
static void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    MVMStaticFrameBody *static_frame_body = &static_frame->body;
    MVMCompUnit        *cu                = static_frame_body->cu;

    /* Ensure the frame is fully deserialized. */
    if (!static_frame_body->fully_deserialized)
        MVM_bytecode_finish_frame(tc, cu, static_frame, 0);

    /* If we never invoked this compilation unit before, and we have spesh
     * enabled, we might either have no spesh log or a nearly full one. This
     * will cause problems with gathering data to OSR hot loops. */
    if (!cu->body.invoked) {
        cu->body.invoked = 1;
        if (tc->instance->spesh_enabled)
            MVM_spesh_log_new_compunit(tc);
    }

    /* Take compilation unit lock, to make sure we don't race to do the
     * frame preparation/verification work. */
    MVMROOT(tc, cu, {
    MVMROOT(tc, static_frame, {
        MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)cu->body.deserialize_frame_mutex);
    });
    });

    if (static_frame->body.instrumentation_level == 0) {
        /* Work size is number of locals/registers plus size of the maximum
        * call site argument list. */
        static_frame_body->work_size = sizeof(MVMRegister) *
            (static_frame_body->num_locals + static_frame_body->cu->body.max_callsite_size);

        /* Validate the bytecode. */
        MVM_validate_static_frame(tc, static_frame);

        /* Compute work area initial state that we can memcpy into place each
         * time. */
        if (static_frame_body->num_locals)
            static_frame_body->work_initial = MVM_frame_initial_work(tc,
                static_frame_body->local_types,
                static_frame_body->num_locals);

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

        /* Allocate the frame's spesh data structure; do it in gen2, both for
         * the sake of not triggering GC here to avoid a deadlock risk, but
         * also because then it can be ssigned into the gen2 static frame
         * without causing it to become an inter-gen root. */
        MVM_gc_allocate_gen2_default_set(tc);
        MVM_ASSIGN_REF(tc, &(static_frame->common.header), static_frame_body->spesh,
            MVM_repr_alloc_init(tc, tc->instance->StaticFrameSpesh));
        MVM_gc_allocate_gen2_default_clear(tc);
    }

    /* Unlock, now we're finished. */
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.deserialize_frame_mutex);
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
    else if (tc->instance->cross_thread_write_logging)
        MVM_cross_thread_write_instrument(tc, static_frame);
    else if (tc->instance->coverage_logging)
        MVM_line_coverage_instrument(tc, static_frame);
    else if (tc->instance->debugserver)
        MVM_breakpoint_instrument(tc, static_frame);
    else
        /* XXX uninstrumenting is currently turned off, due to multithreading
         * woes. If you add an instrumentation that has to be "turned off"
         * again at some point, a solution for this problem must be found. */
        MVM_profile_ensure_uninstrumented(tc, static_frame);
}

/* Called when the GC destroys a frame. Since the frame may have been alive as
 * part of a continuation that was taken but never invoked, we should check
 * things normally cleaned up on return don't need cleaning up also. */
void MVM_frame_destroy(MVMThreadContext *tc, MVMFrame *frame) {
    if (frame->work) {
        MVM_args_proc_cleanup(tc, &frame->params);
        MVM_fixed_size_free(tc, tc->instance->fsa, frame->allocd_work,
            frame->work);
        if (frame->extra) {
            MVMFrameExtra *e = frame->extra;
            if (e->continuation_tags)
                MVM_continuation_free_tags(tc, frame);
            MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMFrameExtra), e);
        }
    }
    if (frame->env)
        MVM_fixed_size_free(tc, tc->instance->fsa, frame->allocd_env, frame->env);
}

/* Creates a frame for usage as a context only, possibly forcing all of the
 * static lexicals to be deserialized if it's used for auto-close purposes. */
static MVMFrame * create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref, MVMint32 autoclose) {
    MVMFrame *frame;

    MVMROOT2(tc, static_frame, code_ref, {
        /* If the frame was never invoked before, need initial calculations
         * and verification. */
         if (static_frame->body.instrumentation_level == 0)
             instrumentation_level_barrier(tc, static_frame);

        frame = MVM_gc_allocate_frame(tc);
    });

    /* Set static frame and code ref. */
    MVM_ASSIGN_REF(tc, &(frame->header), frame->static_info, static_frame);
    MVM_ASSIGN_REF(tc, &(frame->header), frame->code_ref, code_ref);

    /* Allocate space for lexicals, copying the default lexical environment
     * into place and, if we're auto-closing, making sure anything we'd clone
     * is vivified to prevent the clone (which is what creates the correct
     * BEGIN/INIT semantics). */
    if (static_frame->body.env_size) {
        frame->env = MVM_fixed_size_alloc(tc, tc->instance->fsa, static_frame->body.env_size);
        frame->allocd_env = static_frame->body.env_size;
        if (autoclose) {
            MVMROOT2(tc, frame, static_frame, {
                MVMuint16 i;
                MVMuint16 num_lexicals = static_frame->body.num_lexicals;
                for (i = 0; i < num_lexicals; i++) {
                    if (!static_frame->body.static_env[i].o && static_frame->body.static_env_flags[i] == 1) {
                        MVMint32 scid;
                        MVMint32 objid;
                        if (MVM_bytecode_find_static_lexical_scref(tc, static_frame->body.cu,
                                static_frame, i, &scid, &objid)) {
                            MVMObject *resolved;
                            MVMSerializationContext *sc = MVM_sc_get_sc(tc, static_frame->body.cu, scid);

                            if (sc == NULL)
                                MVM_exception_throw_adhoc(tc,
                                    "SC not yet resolved; lookup failed");

                            resolved = MVM_sc_get_object(tc, sc, objid);

                            MVM_ASSIGN_REF(tc, &(static_frame->common.header),
                                static_frame->body.static_env[i].o,
                                resolved);
                        }
                    }
                }
            });
        }
        memcpy(frame->env, static_frame->body.static_env, static_frame->body.env_size);
    }

    return frame;
}

/* Creates a frame that is suitable for deserializing a context into. Starts
 * with a ref count of 1 due to being held by an SC. */
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref) {
    return create_context_only(tc, static_frame, code_ref, 0);
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
    MVMROOT(tc, needed, {
        result = create_context_only(tc, needed, (MVMObject *)needed->body.static_code, 1);
    });
    if (needed->body.outer) {
        /* See if the static code object has an outer. */
        MVMCode *outer_code = needed->body.outer->body.static_code;
        if (outer_code->body.outer &&
                outer_code->body.outer->static_info->body.bytecode == needed->body.bytecode) {
            /* Yes, just take it. */
            MVM_ASSIGN_REF(tc, &(result->header), result->outer, outer_code->body.outer);
        }
        else {
            /* Otherwise, recursively auto-close. */
            MVMROOT(tc, result, {
                MVMFrame *ac = autoclose(tc, needed->body.outer);
                MVM_ASSIGN_REF(tc, &(result->header), result->outer, ac);
            });
        }
    }
    return result;
}

/* Obtains memory for a frame on the thread-local call stack. */
static MVMFrame * allocate_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                                 MVMSpeshCandidate *spesh_cand, MVMint32 heap) {
    MVMFrame *frame;
    MVMint32  env_size, work_size, num_locals;
    MVMStaticFrameBody *static_frame_body;
    MVMJitCode *jitcode;

    if (heap) {
        /* Allocate frame on the heap. We know it's already zeroed. */
        MVMROOT(tc, static_frame, {
            if (tc->cur_frame)
                MVM_frame_force_to_heap(tc, tc->cur_frame);
            frame = MVM_gc_allocate_frame(tc);
        });
    }
    else {
        /* Allocate the frame on the call stack. */
        MVMCallStackRegion *stack = tc->stack_current;
        if (stack->alloc + sizeof(MVMFrame) >= stack->alloc_limit)
            stack = MVM_callstack_region_next(tc);
        frame = (MVMFrame *)stack->alloc;
        stack->alloc += sizeof(MVMFrame);

        /* Ensure collectable header flags and owner are zeroed, which means we'll
         * never try to mark or root the frame. */
        frame->header.flags = 0;
        frame->header.owner = 0;

        /* Current arguments callsite must be NULL as it's used in GC. Extra must
         * be NULL so we know we don't have it. Flags should be zeroed. */
        frame->cur_args_callsite = NULL;
        frame->extra = NULL;
        frame->flags = 0;
    }

    /* Allocate space for lexicals and work area. */
    static_frame_body = &(static_frame->body);
    env_size = spesh_cand ? spesh_cand->env_size : static_frame_body->env_size;

    jitcode = spesh_cand ? spesh_cand->jitcode : NULL;
    num_locals = jitcode && jitcode->local_types ? jitcode->num_locals :
        (spesh_cand ? spesh_cand->num_locals : static_frame_body->num_locals);
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
        if (spesh_cand) {
            /* Allocate zeroed memory. Spesh makes sure we have VMNull setup in
             * the places we need it. */
            frame->work = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, work_size);
        }
        else {
            /* Copy frame template with VMNulls in to place. */
            frame->work = MVM_fixed_size_alloc(tc, tc->instance->fsa, work_size);
            memcpy(frame->work, static_frame_body->work_initial,
                sizeof(MVMRegister) * static_frame_body->num_locals);
        }
        frame->allocd_work = work_size;

        /* Calculate args buffer position. */
        frame->args = frame->work + num_locals;
    }
    else {
        frame->work = NULL;
        frame->allocd_work = 0;
    }

    /* Set static frame and caller before we let this frame escape and the GC
     * see it. */
    frame->static_info = static_frame;
    frame->caller = tc->cur_frame;

    /* Assign a sequence nr */
    frame->sequence_nr = tc->next_frame_nr++;

    return frame;
}

/* Obtains memory for a frame on the heap. */
static MVMFrame * allocate_heap_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                                      MVMSpeshCandidate *spesh_cand) {
    MVMFrame *frame;
    MVMint32  env_size, work_size;
    MVMStaticFrameBody *static_frame_body;

    /* Allocate the frame. */
    MVMROOT(tc, static_frame, {
        frame = MVM_gc_allocate_frame(tc);
    });

    /* Allocate space for lexicals and work area. */
    static_frame_body = &(static_frame->body);
    env_size = spesh_cand ? spesh_cand->env_size : static_frame_body->env_size;
    if (env_size) {
        frame->env = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, env_size);
        frame->allocd_env = env_size;
    }
    work_size = spesh_cand ? spesh_cand->work_size : static_frame_body->work_size;
    if (work_size) {
        /* Fill up all object registers with a pointer to our VMNull object */
        if (spesh_cand && spesh_cand->local_types) {
            MVMuint32 num_locals = spesh_cand->num_locals;
            MVMuint16 *local_types = spesh_cand->local_types;
            MVMuint32 i;
            frame->work = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, work_size);
            for (i = 0; i < num_locals; i++)
                if (local_types[i] == MVM_reg_obj)
                    frame->work[i].o = tc->instance->VMNull;
        }
        else {
            frame->work = MVM_fixed_size_alloc(tc, tc->instance->fsa, work_size);
            memcpy(frame->work, static_frame_body->work_initial,
                sizeof(MVMRegister) * static_frame_body->num_locals);
        }
        frame->allocd_work = work_size;

        /* Calculate args buffer position. */
        frame->args = frame->work + (spesh_cand
            ? spesh_cand->num_locals
            : static_frame_body->num_locals);
    }

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
    MVMuint8 *chosen_bytecode;
    MVMStaticFrameSpesh *spesh;

    /* If the frame was never invoked before, or never before at the current
     * instrumentation level, we need to trigger the instrumentation level
     * barrier. */
    if (static_frame->body.instrumentation_level != tc->instance->instrumentation_level) {
        MVMROOT3(tc, static_frame, code_ref, outer, {
            instrumentation_level_barrier(tc, static_frame);
        });
    }

    /* Ensure we have an outer if needed. This is done ahead of allocating the
     * new frame, since an autoclose will force the callstack on to the heap. */
    if (outer) {
        /* We were provided with an outer frame and it will already have had
         * its reference count incremented; just ensure that it is based on the
         * correct static frame (compare on bytecode address to cope with
         * nqp::freshcoderef). */
        if (MVM_UNLIKELY(outer->static_info->body.orig_bytecode != static_frame->body.outer->body.orig_bytecode)) {
            char *frame_cuuid = MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid);
            char *frame_name;
            char *outer_cuuid = MVM_string_utf8_encode_C_string(tc, outer->static_info->body.cuuid);
            char *outer_name;
            char *frame_outer_cuuid = MVM_string_utf8_encode_C_string(tc, static_frame->body.outer->body.cuuid);
            char *frame_outer_name;

            char *waste[7] = { frame_cuuid, outer_cuuid, frame_outer_cuuid, NULL, NULL, NULL, NULL };
            int waste_counter = 3;

            if (static_frame->body.name) {
                frame_name = MVM_string_utf8_encode_C_string(tc, static_frame->body.name);
                waste[waste_counter++] = frame_name;
            }
            else {
                frame_name = "<anonymous static frame>";
            }

            if (outer->static_info->body.name) {
                outer_name = MVM_string_utf8_encode_C_string(tc, outer->static_info->body.name);
                waste[waste_counter++] = outer_name;
            }
            else {
                outer_name = "<anonymous static frame>";
            }

            if (static_frame->body.outer->body.name) {
                frame_outer_name = MVM_string_utf8_encode_C_string(tc, static_frame->body.outer->body.name);
                waste[waste_counter++] = frame_outer_name;
            }
            else {
                frame_outer_name = "<anonymous static frame>";
            }

            MVM_exception_throw_adhoc_free(tc, waste,
                "When invoking %s '%s', provided outer frame %p (%s '%s') does not match expected static frame %p (%s '%s')",
                frame_cuuid,
                frame_name,
                outer->static_info,
                outer_cuuid,
                outer_name,
                static_frame->body.outer,
                frame_outer_cuuid,
                frame_outer_name);
        }
    }
    else if (static_frame->body.static_code) {
        MVMCode *static_code = static_frame->body.static_code;
        if (static_code->body.outer) {
            /* We're lacking an outer, but our static code object may have one.
            * This comes up in the case of cloned protoregexes, for example. */
            outer = static_code->body.outer;
        }
        else if (static_frame->body.outer) {
            /* Auto-close, and cache it in the static frame. */
            MVMROOT2(tc, static_frame, code_ref, {
                MVM_frame_force_to_heap(tc, tc->cur_frame);
                outer = autoclose(tc, static_frame->body.outer);
                MVM_ASSIGN_REF(tc, &(static_code->common.header),
                    static_code->body.outer, outer);
            });
        }
        else {
            outer = NULL;
        }
    }

    /* See if any specializations apply. */
    spesh = static_frame->body.spesh;
    if (spesh_cand < 0)
        spesh_cand = MVM_spesh_arg_guard_run(tc, spesh->body.spesh_arg_guard,
            callsite, args, NULL);
#if MVM_SPESH_CHECK_PRESELECTION
    else {
        MVMint32 certain = -1;
        MVMint32 correct = MVM_spesh_arg_guard_run(tc, spesh->body.spesh_arg_guard,
            callsite, args, &certain);
        if (spesh_cand != correct && spesh_cand != certain) {
            fprintf(stderr, "Inconsistent spesh preselection of '%s' (%s): got %d, not %d\n",
                MVM_string_utf8_encode_C_string(tc, static_frame->body.name),
                MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid),
                spesh_cand, correct);
            MVM_dump_backtrace(tc);
        }
    }
#endif
    if (spesh_cand >= 0) {
        MVMSpeshCandidate *chosen_cand = spesh->body.spesh_candidates[spesh_cand];
        if (static_frame->body.allocate_on_heap) {
            MVMROOT3(tc, static_frame, code_ref, outer, {
                frame = allocate_frame(tc, static_frame, chosen_cand, 1);
            });
        }
        else {
            frame = allocate_frame(tc, static_frame, chosen_cand, 0);
            frame->spesh_correlation_id = 0;
        }
        if (chosen_cand->jitcode) {
            chosen_bytecode = chosen_cand->jitcode->bytecode;
            frame->jit_entry_label = chosen_cand->jitcode->labels[0];
        }
        else {
            chosen_bytecode = chosen_cand->bytecode;
        }
        frame->effective_spesh_slots = chosen_cand->spesh_slots;
        frame->spesh_cand = chosen_cand;
    }
    else {
        MVMint32 on_heap = static_frame->body.allocate_on_heap;
        if (on_heap) {
            MVMROOT3(tc, static_frame, code_ref, outer, {
                frame = allocate_frame(tc, static_frame, NULL, 1);
            });
        }
        else {
            frame = allocate_frame(tc, static_frame, NULL, 0);
            frame->spesh_cand = NULL;
            frame->effective_spesh_slots = NULL;
            frame->spesh_correlation_id = 0;
        }
        chosen_bytecode = static_frame->body.bytecode;

        /* If we should be spesh logging, set the correlation ID. */
        if (tc->instance->spesh_enabled && tc->spesh_log && static_frame->body.bytecode_size < MVM_SPESH_MAX_BYTECODE_SIZE) {
            if (spesh->body.spesh_entries_recorded++ < MVM_SPESH_LOG_LOGGED_ENOUGH) {
                MVMint32 id = ++tc->spesh_cid;
                frame->spesh_correlation_id = id;
                MVMROOT3(tc, static_frame, code_ref, outer, {
                    if (on_heap) {
                        MVMROOT(tc, frame, {
                            MVM_spesh_log_entry(tc, id, static_frame, callsite);
                        });
                    }
                    else {
                        MVMROOT2(tc, frame->caller, frame->static_info, {
                            MVM_spesh_log_entry(tc, id, static_frame, callsite);
                        });
                    }
                });
            }
        }
    }

    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;

    /* Outer. */
    frame->outer = outer;

    /* Initialize argument processing. */
    MVM_args_proc_init(tc, &frame->params, callsite, args);

    MVM_jit_code_trampoline(tc);

    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    tc->current_frame_nr = frame->sequence_nr;
    *(tc->interp_cur_op) = chosen_bytecode;
    *(tc->interp_bytecode_start) = chosen_bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame->body.cu;

    /* If we need to do so, make clones of things in the lexical environment
     * that need it. Note that we do this after tc->cur_frame became the
     * current frame, to make sure these new objects will certainly get
     * marked if GC is triggered along the way. */
    if (static_frame->body.has_state_vars) {
        /* Drag everything out of static_frame_body before we start,
         * as GC action may invalidate it. */
        MVMRegister *env       = static_frame->body.static_env;
        MVMuint8    *flags     = static_frame->body.static_env_flags;
        MVMint64     numlex    = static_frame->body.num_lexicals;
        MVMRegister *state     = NULL;
        MVMint64     state_act = 0; /* 0 = none so far, 1 = first time, 2 = later */
        MVMint64 i;
        MVMROOT(tc, frame, {
            for (i = 0; i < numlex; i++) {
                if (flags[i] == 2) {
                    redo_state:
                    switch (state_act) {
                    case 0:
                        if (MVM_UNLIKELY(!frame->code_ref))
                            MVM_exception_throw_adhoc(tc,
                                "Frame must have code-ref to have state variables");
                        state = ((MVMCode *)frame->code_ref)->body.state_vars;
                        if (state) {
                            /* Already have state vars; pull them from this. */
                            state_act = 2;
                        }
                        else {
                            /* Allocate storage for state vars. */
                            state = (MVMRegister *)MVM_calloc(1, frame->static_info->body.env_size);
                            ((MVMCode *)frame->code_ref)->body.state_vars = state;
                            state_act = 1;

                            /* Note that this frame should run state init code. */
                            frame->flags |= MVM_FRAME_FLAG_STATE_INIT;
                        }
                        goto redo_state;
                    case 1: {
                        MVMObject *cloned = MVM_repr_clone(tc, env[i].o);
                        frame->env[i].o = cloned;
                        MVM_ASSIGN_REF(tc, &(frame->code_ref->header), state[i].o, cloned);
                        break;
                    }
                    case 2:
                        frame->env[i].o = state[i].o;
                        break;
                    }
                }
            }
        });
    }
}

/* Moves the specified frame from the stack and on to the heap. Must only
 * be called if the frame is not already there. Use MVM_frame_force_to_heap
 * when not sure. */
MVMFrame * MVM_frame_move_to_heap(MVMThreadContext *tc, MVMFrame *frame) {
    /* To keep things simple, we'll promote the entire stack. */
    MVMFrame *cur_to_promote = tc->cur_frame;
    MVMFrame *new_cur_frame = NULL;
    MVMFrame *update_caller = NULL;
    MVMFrame *result = NULL;
    MVM_CHECK_CALLER_CHAIN(tc, cur_to_promote);
    MVMROOT3(tc, new_cur_frame, update_caller, result, {
        while (cur_to_promote) {
            /* Allocate a heap frame. */
            MVMFrame *promoted = MVM_gc_allocate_frame(tc);

            /* Bump heap promotion counter, to encourage allocating this kind
             * of frame directly on the heap in the future. If the frame was
             * entered at least 50 times, and over 80% of the entries lead to
             * an eventual heap promotion, them we'll mark it to be allocated
             * right away on the heap. Note that entries is only bumped when
             * spesh logging is taking place, so we only bump the number of
             * heap promotions in that case too. */
            MVMStaticFrame *sf = cur_to_promote->static_info;
            if (!sf->body.allocate_on_heap && cur_to_promote->spesh_correlation_id) {
                MVMuint32 promos = sf->body.spesh->body.num_heap_promotions++;
                MVMuint32 entries = sf->body.spesh->body.spesh_entries_recorded;
                if (entries > 50 && promos > (4 * entries) / 5)
                    sf->body.allocate_on_heap = 1;
            }

            /* Copy current frame's body to it. */
            memcpy(
                (char *)promoted + sizeof(MVMCollectable),
                (char *)cur_to_promote + sizeof(MVMCollectable),
                sizeof(MVMFrame) - sizeof(MVMCollectable));

            /* Update caller of previously promoted frame, if any. This is the
             * only reference that might point to a non-heap frame. */
            if (update_caller) {
                MVM_ASSIGN_REF(tc, &(update_caller->header),
                    update_caller->caller, promoted);
            }

            /* If we're the first time through the lopo, then we're instead
             * replacing the current stack top. Note we do it at the end,
             * so that the GC can still walk unpromoted frames if it runs
             * in this loop. */
            else {
                new_cur_frame = promoted;
            }

            /* If the frame we're promoting was in the active handlers list,
             * update the address there. */
            if (tc->active_handlers) {
                MVMActiveHandler *ah = tc->active_handlers;
                while (ah) {
                    if (ah->frame == cur_to_promote)
                        ah->frame = promoted;
                    ah = ah->next_handler;
                }
            }

            /* If we're replacing the frame we were asked to promote, that will
             * become our result. */
            if (cur_to_promote == frame)
                result = promoted;

            /* Check if there's a caller, or if we reached the end of the
             * chain. */
            if (cur_to_promote->caller) {
                /* If the caller is on the stack then it needs promotion too.
                 * If not, we're done. */
                if (MVM_FRAME_IS_ON_CALLSTACK(tc, cur_to_promote->caller)) {
                    /* Clear caller in promoted frame, to avoid a heap -> stack
                     * reference if we GC during this loop. */
                    promoted->caller = NULL;
                    update_caller = promoted;
                    cur_to_promote = cur_to_promote->caller;
                }
                else {
                    if (cur_to_promote == tc->thread_entry_frame)
                        tc->thread_entry_frame = promoted;
                    cur_to_promote = NULL;
                }
            }
            else {
                /* End of caller chain; check if we promoted the entry
                 * frame */
                if (cur_to_promote == tc->thread_entry_frame)
                    tc->thread_entry_frame = promoted;
                cur_to_promote = NULL;
            }
        }
    });
    MVM_CHECK_CALLER_CHAIN(tc, new_cur_frame);

    /* All is promoted. Update thread's current frame and reset the thread
     * local callstack. */
    tc->cur_frame = new_cur_frame;
    MVM_callstack_reset(tc);

    /* Hand back new location of promoted frame. */
    if (!result)
        MVM_panic(1, "Failed to find frame to promote on call stack");
    return result;
}

/* This function is to be used by the debugserver if a thread is currently
 * blocked. */
MVMFrame * MVM_frame_debugserver_move_to_heap(MVMThreadContext *tc, MVMThreadContext *owner, MVMFrame *frame) {
    /* To keep things simple, we'll promote the entire stack. */
    MVMFrame *cur_to_promote = owner->cur_frame;
    MVMFrame *new_cur_frame = NULL;
    MVMFrame *update_caller = NULL;
    MVMFrame *result = NULL;
    MVM_CHECK_CALLER_CHAIN(tc, cur_to_promote);
    MVMROOT3(tc, new_cur_frame, update_caller, result, {
        while (cur_to_promote) {
            /* Allocate a heap frame. */
            MVMFrame *promoted = MVM_gc_allocate_frame(tc);

            /* Bump heap promotion counter, to encourage allocating this kind
             * of frame directly on the heap in the future. If the frame was
             * entered at least 50 times, and over 80% of the entries lead to
             * an eventual heap promotion, them we'll mark it to be allocated
             * right away on the heap. Note that entries is only bumped when
             * spesh logging is taking place, so we only bump the number of
             * heap promotions in that case too. */
            MVMStaticFrame *sf = cur_to_promote->static_info;
            if (!sf->body.allocate_on_heap && cur_to_promote->spesh_correlation_id) {
                MVMuint32 promos = sf->body.spesh->body.num_heap_promotions++;
                MVMuint32 entries = sf->body.spesh->body.spesh_entries_recorded;
                if (entries > 50 && promos > (4 * entries) / 5)
                    sf->body.allocate_on_heap = 1;
            }

            /* Copy current frame's body to it. */
            memcpy(
                (char *)promoted + sizeof(MVMCollectable),
                (char *)cur_to_promote + sizeof(MVMCollectable),
                sizeof(MVMFrame) - sizeof(MVMCollectable));

            /* Update caller of previously promoted frame, if any. This is the
             * only reference that might point to a non-heap frame. */
            if (update_caller) {
                MVM_ASSIGN_REF(tc, &(update_caller->header),
                    update_caller->caller, promoted);
            }

            /* If we're the first time through the lopo, then we're instead
             * replacing the current stack top. Note we do it at the end,
             * so that the GC can still walk unpromoted frames if it runs
             * in this loop. */
            else {
                new_cur_frame = promoted;
            }

            /* If the frame we're promoting was in the active handlers list,
             * update the address there. */
            if (owner->active_handlers) {
                MVMActiveHandler *ah = owner->active_handlers;
                while (ah) {
                    if (ah->frame == cur_to_promote)
                        ah->frame = promoted;
                    ah = ah->next_handler;
                }
            }

            /* If we're replacing the frame we were asked to promote, that will
             * become our result. */
            if (cur_to_promote == frame)
                result = promoted;

            /* Check if there's a caller, or if we reached the end of the
             * chain. */
            if (cur_to_promote->caller) {
                /* If the caller is on the stack then it needs promotion too.
                 * If not, we're done. */
                if (MVM_FRAME_IS_ON_CALLSTACK(tc, cur_to_promote->caller)) {
                    /* Clear caller in promoted frame, to avoid a heap -> stack
                     * reference if we GC during this loop. */
                    promoted->caller = NULL;
                    update_caller = promoted;
                    cur_to_promote = cur_to_promote->caller;
                }
                else {
                    if (cur_to_promote == owner->thread_entry_frame)
                        owner->thread_entry_frame = promoted;
                    cur_to_promote = NULL;
                }
            }
            else {
                /* End of caller chain; check if we promoted the entry
                 * frame */
                if (cur_to_promote == owner->thread_entry_frame)
                    owner->thread_entry_frame = promoted;
                cur_to_promote = NULL;
            }
        }
    });
    MVM_CHECK_CALLER_CHAIN(tc, new_cur_frame);

    /* All is promoted. Update thread's current frame and reset the thread
     * local callstack. */
    owner->cur_frame = new_cur_frame;
    MVM_callstack_reset(owner);

    /* Hand back new location of promoted frame. */
    if (!result)
        MVM_panic(1, "Failed to find frame to promote on foreign thread's call stack");
    return result;
}

/* Creates a frame for de-optimization purposes. */
MVMFrame * MVM_frame_create_for_deopt(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                                      MVMCode *code_ref) {
    MVMFrame *frame;
    MVMROOT2(tc, static_frame, code_ref, {
        frame = allocate_heap_frame(tc, static_frame, NULL);
    });
    MVM_ASSIGN_REF(tc, &(frame->header), frame->static_info, static_frame);
    MVM_ASSIGN_REF(tc, &(frame->header), frame->code_ref, code_ref);
    MVM_ASSIGN_REF(tc, &(frame->header), frame->outer, code_ref->body.outer);
    return frame;
}

/* Removes a single frame, as part of a return or unwind. Done after any exit
 * handler has already been run. */
static MVMuint64 remove_one_frame(MVMThreadContext *tc, MVMuint8 unwind) {
    MVMFrame *returner = tc->cur_frame;
    MVMFrame *caller   = returner->caller;
    MVM_ASSERT_NOT_FROMSPACE(tc, caller);

    /* Clear up any extra frame data. */
    if (returner->extra) {
        MVMFrameExtra *e = returner->extra;
        if (e->continuation_tags)
            MVM_continuation_free_tags(tc, returner);
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMFrameExtra), e);
        returner->extra = NULL;
    }

    /* Clean up frame working space. */
    if (returner->work) {
        MVM_args_proc_cleanup(tc, &returner->params);
        MVM_fixed_size_free(tc, tc->instance->fsa, returner->allocd_work,
            returner->work);
    }

    /* If it's a call stack frame, remove it from the stack. */
    if (MVM_FRAME_IS_ON_CALLSTACK(tc, returner)) {
        MVMCallStackRegion *stack = tc->stack_current;
        stack->alloc = (char *)returner;
        if ((char *)stack->alloc - sizeof(MVMCallStackRegion) == (char *)stack)
            MVM_callstack_region_prev(tc);
        if (returner->env)
            MVM_fixed_size_free(tc, tc->instance->fsa, returner->allocd_env, returner->env);
    }

    /* Otherwise, NULL  out ->work, to indicate the frame is no longer in
     * dynamic scope. This is used by the GC to avoid marking stuff (this is
     * needed for safety as otherwise we'd read freed memory), as well as by
     * exceptions to ensure the target of an exception throw is indeed still
     * in dynamic scope. */
    else {
        returner->work = NULL;
    }

    /* Switch back to the caller frame if there is one. */
    if (caller && returner != tc->thread_entry_frame) {

       if (tc->jit_return_address != NULL) {
            /* on a JIT frame, exit to interpreter afterwards */
            MVMJitCode *jitcode = returner->spesh_cand->jitcode;
            MVM_jit_code_set_current_position(tc, jitcode, returner, jitcode->exit_label);
            /* given that we might throw in the special-return, act as if we've
             * left the current frame (which is true) */
            tc->jit_return_address = NULL;
        }

        tc->cur_frame = caller;
        tc->current_frame_nr = caller->sequence_nr;

        *(tc->interp_cur_op) = caller->return_address;
        *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(caller);
        *(tc->interp_reg_base) = caller->work;
        *(tc->interp_cu) = caller->static_info->body.cu;

        /* Handle any special return hooks. */
        if (caller->extra) {
            MVMFrameExtra *e = caller->extra;
            if (e->special_return || e->special_unwind) {
                MVMSpecialReturn  sr  = e->special_return;
                MVMSpecialReturn  su  = e->special_unwind;
                void             *srd = e->special_return_data;
                e->special_return           = NULL;
                e->special_unwind           = NULL;
                e->special_return_data      = NULL;
                e->mark_special_return_data = NULL;
                if (unwind && su)
                    su(tc, srd);
                else if (!unwind && sr)
                    sr(tc, srd);
            }
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

        if (caller->return_type == MVM_RETURN_OBJ) {
            result = caller->return_value->o;
            if (!result)
                result = tc->instance->VMNull;
        }
        else {
            MVMROOT(tc, cur_frame, {
                switch (caller->return_type) {
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
                        result = tc->instance->VMNull;
                }
            });
        }

        handler = MVM_frame_find_invokee(tc, hll->exit_handler, NULL);
        two_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ);
        MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, two_args_callsite);
        cur_frame->args[0].o = cur_frame->code_ref;
        cur_frame->args[1].o = result;
        MVM_frame_special_return(tc, cur_frame, remove_after_handler, NULL, NULL, NULL);
        cur_frame->flags |= MVM_FRAME_FLAG_EXIT_HAND_RUN;
        STABLE(handler)->invoke(tc, handler, two_args_callsite, cur_frame->args);
        return 1;
    }
    else {
        /* No exit handler, so a straight return. */
        return remove_one_frame(tc, 0);
    }
}

/* Try a return from the current frame; skip running any exit handlers. */
MVMuint64 MVM_frame_try_return_no_exit_handlers(MVMThreadContext *tc) {
    return remove_one_frame(tc, 0);
}

/* Unwinds execution state to the specified frame, placing control flow at either
 * an absolute or relative (to start of target frame) address and optionally
 * setting a returned result. */
typedef struct {
    MVMFrame  *frame;
    MVMuint8  *abs_addr;
    MVMuint32  rel_addr;
    void      *jit_return_label;
} MVMUnwindData;
static void mark_unwind_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    MVMUnwindData *ud  = (MVMUnwindData *)frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &(ud->frame));
}
static void continue_unwind(MVMThreadContext *tc, void *sr_data) {
    MVMUnwindData *ud  = (MVMUnwindData *)sr_data;
    MVMFrame *frame    = ud->frame;
    MVMuint8 *abs_addr = ud->abs_addr;
    MVMuint32 rel_addr = ud->rel_addr;
    void *jit_return_label = ud->jit_return_label;
    MVM_free(sr_data);
    MVM_frame_unwind_to(tc, frame, abs_addr, rel_addr, NULL, jit_return_label);
}
void MVM_frame_unwind_to(MVMThreadContext *tc, MVMFrame *frame, MVMuint8 *abs_addr,
                         MVMuint32 rel_addr, MVMObject *return_value, void *jit_return_label) {
    while (tc->cur_frame != frame) {
        MVMFrame *cur_frame = tc->cur_frame;
        if (cur_frame->static_info->body.has_exit_handler &&
                !(cur_frame->flags & MVM_FRAME_FLAG_EXIT_HAND_RUN)) {
            /* We're unwinding a frame with an exit handler. Thus we need to
             * pause the unwind, run the exit handler, and keep enough info
             * around in order to finish up the unwind afterwards. */
            MVMHLLConfig *hll    = MVM_hll_current(tc);
            MVMFrame     *caller;
            MVMObject    *handler;
            MVMCallsite *two_args_callsite;

            /* Force the frame onto the heap, since we'll reference it from the
             * unwind data. */
            MVMROOT3(tc, frame, cur_frame, return_value, {
                frame = MVM_frame_force_to_heap(tc, frame);
                cur_frame = tc->cur_frame;
            });

            caller = cur_frame->caller;
            if (!caller)
                MVM_exception_throw_adhoc(tc, "Entry point frame cannot have an exit handler");
            if (cur_frame == tc->thread_entry_frame)
                MVM_exception_throw_adhoc(tc, "Thread entry point frame cannot have an exit handler");

            handler = MVM_frame_find_invokee(tc, hll->exit_handler, NULL);
            two_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_TWO_OBJ);
            MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, two_args_callsite);
            cur_frame->args[0].o = cur_frame->code_ref;
            cur_frame->args[1].o = tc->instance->VMNull;
            {
                MVMUnwindData *ud = MVM_malloc(sizeof(MVMUnwindData));
                ud->frame = frame;
                ud->abs_addr = abs_addr;
                ud->rel_addr = rel_addr;
                ud->jit_return_label = jit_return_label;
                if (return_value)
                    MVM_exception_throw_adhoc(tc, "return_value + exit_handler case NYI");
                MVM_frame_special_return(tc, cur_frame, continue_unwind, NULL, ud,
                    mark_unwind_data);
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

    if (jit_return_label) {
        MVM_jit_code_set_current_position(tc, tc->cur_frame->spesh_cand->jitcode, tc->cur_frame, jit_return_label);
    }

    if (return_value)
        MVM_args_set_result_obj(tc, return_value, 1);
}

/* Gets a code object for a frame, lazily deserializing it if needed. */
MVMObject * MVM_frame_get_code_object(MVMThreadContext *tc, MVMCode *code) {
    if (MVM_UNLIKELY(REPR(code)->ID != MVM_REPR_ID_MVMCode))
        MVM_exception_throw_adhoc(tc, "getcodeobj needs a code ref");

    if (!code->body.code_object) {
        MVMStaticFrame *sf = code->body.sf;
        if (sf->body.code_obj_sc_dep_idx > 0) {
            MVMObject *resolved;
            MVMSerializationContext *sc = MVM_sc_get_sc(tc, sf->body.cu,
                sf->body.code_obj_sc_dep_idx - 1);
            if (MVM_UNLIKELY(sc == NULL))
                MVM_exception_throw_adhoc(tc,
                    "SC not yet resolved; lookup failed");

            MVMROOT(tc, code, {
                resolved = MVM_sc_get_object(tc, sc, sf->body.code_obj_sc_idx);
            });

            MVM_ASSIGN_REF(tc, &(code->common.header), code->body.code_object,
                resolved);
        }
    }
    return code->body.code_object;
}

/* Given the specified code object, sets its outer to the current scope. */
void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *code_obj = (MVMCode *)code;
    MVMFrame *captured;
    if (MVM_UNLIKELY(REPR(code)->ID != MVM_REPR_ID_MVMCode))
        MVM_exception_throw_adhoc(tc,
            "Can only perform capturelex on object with representation MVMCode");
    MVMROOT(tc, code, {
        captured = MVM_frame_force_to_heap(tc, tc->cur_frame);
    });
    MVM_ASSIGN_REF(tc, &(code->header), code_obj->body.outer, captured);
}

/* This is used for situations in Perl 6 like:
 * supply {
 *     my $x = something();
 *     whenever $supply {
 *         QUIT { $x.foo() }
 *     }
 * }
 * Here, the QUIT may be called without an invocation of the whenever ever
 * having taken place. At the point we closure-clone the whenever block, we
 * will capture_inner the QUIT phaser. This creates a fake outer for the
 * QUIT, but makes *its* outer point to the nearest instance of the relevant
 * static frame on the call stack, so that the QUIT will disocver the correct
 * $x.
 */
void MVM_frame_capture_inner(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *code_obj = (MVMCode *)code;
    MVMFrame *outer;
    MVMROOT(tc, code, {
        MVMStaticFrame *sf_outer = code_obj->body.sf->body.outer;
        MVMROOT(tc, sf_outer, {
            outer = create_context_only(tc, sf_outer, (MVMObject *)sf_outer->body.static_code, 1);
        });
        MVMROOT(tc, outer, {
            MVMFrame *outer_outer = autoclose(tc, sf_outer->body.outer);
            MVM_ASSIGN_REF(tc, &(outer->header), outer->outer, outer_outer);
        });
    });
    MVM_ASSIGN_REF(tc, &(code->header), code_obj->body.outer, outer);
}

/* Given the specified code object, copies it and returns a copy which
 * captures a closure over the current scope. */
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *closure;
    MVMFrame *captured;

    if (MVM_UNLIKELY(REPR(code)->ID != MVM_REPR_ID_MVMCode))
        MVM_exception_throw_adhoc(tc,
            "Can only perform takeclosure on object with representation MVMCode");

    MVMROOT(tc, code, {
        closure = (MVMCode *)REPR(code)->allocate(tc, STABLE(code));
        MVMROOT(tc, closure, {
            captured = MVM_frame_force_to_heap(tc, tc->cur_frame);
        });
    });

    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.sf, ((MVMCode *)code)->body.sf);
    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.name, ((MVMCode *)code)->body.name);
    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.outer, captured);

    MVM_ASSIGN_REF(tc, &(closure->common.header), closure->body.code_object,
        ((MVMCode *)code)->body.code_object);

    return (MVMObject *)closure;
}

/* Vivifies a lexical in a frame. */
MVMObject * MVM_frame_vivify_lexical(MVMThreadContext *tc, MVMFrame *f, MVMuint16 idx) {
    MVMuint8       *flags;
    MVMint16        flag;
    MVMRegister    *static_env;
    MVMuint16       effective_idx = 0;
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
            MVMStaticFrame *isf = f->spesh_cand->inlines[i].sf;
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
        MVMint32 scid, objid;
        if (MVM_bytecode_find_static_lexical_scref(tc, effective_sf->body.cu,
                effective_sf, effective_idx, &scid, &objid)) {
            MVMSerializationContext *sc = MVM_sc_get_sc(tc, effective_sf->body.cu, scid);
            MVMObject *resolved;
            if (sc == NULL)
                MVM_exception_throw_adhoc(tc,
                    "SC not yet resolved; lookup failed");
            MVMROOT(tc, f, {
                resolved = MVM_sc_get_object(tc, sc, objid);
            });
            MVM_ASSIGN_REF(tc, &(effective_sf->common.header),
                effective_sf->body.static_env[effective_idx].o,
                resolved);
        }
    }
    if (flag == 0) {
        MVMObject *viv = static_env[effective_idx].o;
        if (!viv)
            viv = tc->instance->VMNull;
        MVM_ASSIGN_REF(tc, &(f->header), f->env[idx].o, viv);
        return viv;
    }
    else if (flag == 1) {
        MVMObject *viv;
        MVMROOT(tc, f, {
            viv = MVM_repr_clone(tc, static_env[effective_idx].o);
            MVM_ASSIGN_REF(tc, &(f->header), f->env[idx].o, viv);
        });
        return viv;
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
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
            MVMLexicalRegistry *entry;
            MVM_HASH_GET(tc, lexical_names, name, entry)
            if (entry) {
                if (MVM_LIKELY(cur_frame->static_info->body.lexical_types[entry->value] == type)) {
                    MVMRegister *result = &cur_frame->env[entry->value];
                    if (type == MVM_reg_obj && !result->o)
                        MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                    return result;
                }
                else {
                    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                        "Lexical with name '%s' has wrong type",
                            c_name);
                }
            }
        }
        cur_frame = cur_frame->outer;
    }
    if (MVM_UNLIKELY(type != MVM_reg_obj)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "No lexical found with name '%s'",
            c_name);
    }
    return NULL;
}

/* Binds the specified value to the given lexical, finding it along the static
 * chain. */
MVM_PUBLIC void MVM_frame_bind_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type, MVMRegister *value) {
    MVMFrame *cur_frame = tc->cur_frame;
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            MVMLexicalRegistry *entry;
            MVM_HASH_GET(tc, lexical_names, name, entry)
            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == type) {
                    if (type == MVM_reg_obj || type == MVM_reg_str) {
                        MVM_ASSIGN_REF(tc, &(cur_frame->header),
                            cur_frame->env[entry->value].o, value->o);
                    }
                    else {
                        cur_frame->env[entry->value] = *value;
                    }
                    return;
                }
                else {
                    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                        "Lexical with name '%s' has wrong type",
                            c_name);
                }
            }
        }
        cur_frame = cur_frame->outer;
    }
    {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "No lexical found with name '%s'",
            c_name);
    }
}

/* Finds a lexical in the outer frame, throwing if it's not there. */
MVMObject * MVM_frame_find_lexical_by_name_outer(MVMThreadContext *tc, MVMString *name) {
    MVMRegister *r = MVM_frame_find_lexical_by_name_rel(tc, name, tc->cur_frame->outer);
    if (MVM_LIKELY(r != NULL))
        return r->o;
    else {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "No lexical found with name '%s'",
            c_name);
    }
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. Only works if it's an object lexical.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
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
                    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                        "Lexical with name '%s' has wrong type",
                            c_name);
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
                        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                        char *waste[] = { c_name, NULL };
                        MVM_exception_throw_adhoc_free(tc, waste,
                            "Lexical with name '%s' has wrong type",
                                c_name);
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
#if MVM_DYNLEX_CACHE_ENABLED
    MVMint32 next = 0;
    MVMint32 frames = 0;
    MVMuint32 desperation = 0;

    if (fcost+icost > 20)
        desperation = 1;

    while (from && from != to) {
        frames++;
        if (frames >= next) {
            if (!from->extra || !from->extra->dynlex_cache_name || (desperation && frames > 1)) {
                MVMFrameExtra *e = MVM_frame_extra(tc, from);
                MVM_ASSIGN_REF(tc, &(from->header), e->dynlex_cache_name, name);
                e->dynlex_cache_reg  = reg;
                e->dynlex_cache_type = type;
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
#endif
}
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type, MVMFrame *cur_frame, MVMint32 vivify, MVMFrame **found_frame) {
    FILE *dlog = tc->instance->dynvar_log_fh;
    MVMuint32 fcost = 0;  /* frames traversed */
    MVMuint32 icost = 0;  /* inlines traversed */
    MVMuint32 ecost = 0;  /* frames traversed with empty cache */
    MVMuint32 xcost = 0;  /* frames traversed with wrong name */
    char *c_name;
    MVMuint64 start_time;
    MVMuint64 last_time;

    MVMFrame *initial_frame = cur_frame;
    if (MVM_UNLIKELY(!name))
        MVM_exception_throw_adhoc(tc, "Contextual name cannot be null");
    if (dlog) {
        c_name = MVM_string_utf8_encode_C_string(tc, name);
        start_time = uv_hrtime();
        last_time = tc->instance->dynvar_log_lasttime;
    }

    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names;
        MVMSpeshCandidate  *cand = cur_frame->spesh_cand;
        MVMFrameExtra *e;
        /* See if we are inside an inline. Note that this isn't actually
         * correct for a leaf frame, but those aren't inlined and don't
         * use getdynlex for their own lexicals since the compiler already
         * knows where to find them */
        if (cand && cand->num_inlines) {
            if (cand->jitcode) {
                MVMJitCode *jitcode = cand->jitcode;
                void * current_position = MVM_jit_code_get_current_position(tc, jitcode, cur_frame);
                MVMint32 i;

                for (i = MVM_jit_code_get_active_inlines(tc, jitcode, current_position, 0);
                     i < jitcode->num_inlines;
                     i = MVM_jit_code_get_active_inlines(tc, jitcode, current_position, i+1)) {
                    MVMStaticFrame *isf = cand->inlines[i].sf;
                    icost++;
                    if ((lexical_names = isf->body.lexical_names)) {
                        MVMLexicalRegistry *entry;
                        MVM_HASH_GET(tc, lexical_names, name, entry);
                        if (entry) {
                            MVMuint16    lexidx = cand->inlines[i].lexicals_start + entry->value;
                            MVMRegister *result = &cur_frame->env[lexidx];
                            *type = cand->lexical_types[lexidx];
                            if (vivify && *type == MVM_reg_obj && !result->o) {
                                MVMROOT3(tc, cur_frame, initial_frame, name, {
                                    MVM_frame_vivify_lexical(tc, cur_frame, lexidx);
                                });
                            }
                            if (fcost+icost > 1)
                                try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                            if (dlog) {
                                fprintf(dlog, "I %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                                fflush(dlog);
                                MVM_free(c_name);
                                tc->instance->dynvar_log_lasttime = uv_hrtime();
                            }
                            *found_frame = cur_frame;
                            return result;
                        }
                    }
                }
            } else {
                MVMint32 ret_offset = cur_frame->return_address -
                    MVM_frame_effective_bytecode(cur_frame);
                MVMint32 i;
                for (i = 0; i < cand->num_inlines; i++) {
                    icost++;
                    if (ret_offset >= cand->inlines[i].start && ret_offset <= cand->inlines[i].end) {
                        MVMStaticFrame *isf = cand->inlines[i].sf;
                        if ((lexical_names = isf->body.lexical_names)) {
                            MVMLexicalRegistry *entry;
                            MVM_HASH_GET(tc, lexical_names, name, entry);
                            if (entry) {
                                MVMuint16    lexidx = cand->inlines[i].lexicals_start + entry->value;
                                MVMRegister *result = &cur_frame->env[lexidx];
                                *type = cand->lexical_types[lexidx];
                                if (vivify && *type == MVM_reg_obj && !result->o) {
                                    MVMROOT3(tc, cur_frame, initial_frame, name, {
                                        MVM_frame_vivify_lexical(tc, cur_frame, lexidx);
                                    });
                                }
                                if (fcost+icost > 1)
                                  try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                                if (dlog) {
                                    fprintf(dlog, "I %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                                    fflush(dlog);
                                    MVM_free(c_name);
                                    tc->instance->dynvar_log_lasttime = uv_hrtime();
                                }
                                *found_frame = cur_frame;
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* See if we've got it cached at this level. */
        e = cur_frame->extra;
        if (e && e->dynlex_cache_name) {
            if (MVM_string_equal(tc, name, e->dynlex_cache_name)) {
                MVMRegister *result = e->dynlex_cache_reg;
                *type = e->dynlex_cache_type;
                if (fcost+icost > 5)
                    try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                if (dlog) {
                    fprintf(dlog, "C %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                    fflush(dlog);
                    MVM_free(c_name);
                    tc->instance->dynvar_log_lasttime = uv_hrtime();
                }
                *found_frame = cur_frame;
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
                if (vivify && *type == MVM_reg_obj && !result->o) {
                    MVMROOT3(tc, cur_frame, initial_frame, name, {
                        MVM_frame_vivify_lexical(tc, cur_frame, entry->value);
                    });
                }
                if (dlog) {
                    fprintf(dlog, "F %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                    fflush(dlog);
                    MVM_free(c_name);
                    tc->instance->dynvar_log_lasttime = uv_hrtime();
                }
                if (fcost+icost > 1)
                    try_cache_dynlex(tc, initial_frame, cur_frame, name, result, *type, fcost, icost);
                *found_frame = cur_frame;
                return result;
            }
        }
        fcost++;
        cur_frame = cur_frame->caller;
    }
    if (dlog) {
        fprintf(dlog, "N %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
        fflush(dlog);
        MVM_free(c_name);
        tc->instance->dynvar_log_lasttime = uv_hrtime();
    }
    *found_frame = NULL;
    return NULL;
}

MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMFrame *found_frame;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame, 1, &found_frame);
    MVMObject *result = NULL, *result_type = NULL;
    if (lex_reg) {
        switch (MVM_EXPECT(type, MVM_reg_obj)) {
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
                MVM_exception_throw_adhoc(tc, "invalid register type in getdynlex: %d", type);
        }
    }
    return result ? result : tc->instance->VMNull;
}

void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMFrame *found_frame;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame, 0, &found_frame);
    if (!lex_reg) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "No contextual found with name '%s'",
            c_name);
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
            MVM_ASSIGN_REF(tc, &(found_frame->header), lex_reg->s,
                REPR(value)->box_funcs.get_str(tc, STABLE(value), value, OBJECT_BODY(value)));
            break;
        case MVM_reg_obj:
            MVM_ASSIGN_REF(tc, &(found_frame->header), lex_reg->o, value);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "invalid register type in binddynlex");
    }
}

/* Returns the storage unit for the lexical in the specified frame. Does not
 * try to vivify anything - gets exactly what is there. */
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (MVM_LIKELY(lexical_names != NULL)) {
        MVMLexicalRegistry *entry;
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry)
            return &f->env[entry->value];
    }
    {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Frame has no lexical with name '%s'",
            c_name);
    }
}

/* Returns the storage unit for the lexical in the specified frame. */
MVMRegister * MVM_frame_try_get_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name, MVMuint16 type) {
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
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
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry) {
            switch (MVM_EXPECT(f->static_info->body.lexical_types[entry->value], MVM_reg_obj)) {
                case MVM_reg_int64:
                    return MVM_STORAGE_SPEC_BP_INT;
                case MVM_reg_num64:
                    return MVM_STORAGE_SPEC_BP_NUM;
                case MVM_reg_str:
                    return MVM_STORAGE_SPEC_BP_STR;
                case MVM_reg_obj:
                    return MVM_STORAGE_SPEC_BP_NONE;
                case MVM_reg_int8:
                    return MVM_STORAGE_SPEC_BP_INT8;
                case MVM_reg_int16:
                    return MVM_STORAGE_SPEC_BP_INT16;
                case MVM_reg_int32:
                    return MVM_STORAGE_SPEC_BP_INT32;
                case MVM_reg_uint8:
                    return MVM_STORAGE_SPEC_BP_UINT8;
                case MVM_reg_uint16:
                    return MVM_STORAGE_SPEC_BP_UINT16;
                case MVM_reg_uint32:
                    return MVM_STORAGE_SPEC_BP_UINT32;
                case MVM_reg_uint64:
                    return MVM_STORAGE_SPEC_BP_UINT64;
                default:
                {
                    char *c_name  = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                        "Unhandled lexical type '%s' in lexprimspec for '%s'",
                        MVM_reg_get_debug_name(tc, f->static_info->body.lexical_types[entry->value]),
                        c_name);
                }
            }
        }
    }
    {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Frame has no lexical with name '%s'",
            c_name);
    }
}

static MVMObject * find_invokee_internal(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs, MVMInvocationSpec *is) {
    /* Fast path when we have an offset directly into a P6opaque. */
    if (is->code_ref_offset) {
        if (!IS_CONCRETE(code))
            MVM_exception_throw_adhoc(tc, "Can not invoke a code type object");
        code = MVM_p6opaque_read_object(tc, code, is->code_ref_offset);
    }

    /* Otherwise, if there is a class handle, fall back to the slow path
     * lookup, but set up code_ref_offset if applicable. */
    else if (!MVM_is_null(tc, is->class_handle)) {
        MVMRegister dest;
        if (!IS_CONCRETE(code))
            MVM_exception_throw_adhoc(tc, "Can not invoke a code type object");
        if (code->st->REPR->ID == MVM_REPR_ID_P6opaque)
            is->code_ref_offset = MVM_p6opaque_attr_offset(tc, code->st->WHAT,
                is->class_handle, is->attr_name);
        REPR(code)->attr_funcs.get_attribute(tc,
            STABLE(code), code, OBJECT_BODY(code),
            is->class_handle, is->attr_name,
            is->hint, &dest, MVM_reg_obj);
        code = dest.o;
    }

    /* Failing that, it must be an invocation handler. */
    else {
        /* Need to tweak the callsite and args to include the code object
         * being invoked. */
        if (tweak_cs) {
            MVMCallsite *orig = *tweak_cs;
            if (orig->with_invocant) {
                *tweak_cs = orig->with_invocant;
            }
            else {
                MVMCallsite *new   = MVM_calloc(1, sizeof(MVMCallsite));
                MVMint32     fsize = orig->flag_count;
                new->flag_count    = fsize + 1;
                new->arg_flags     = MVM_malloc(new->flag_count * sizeof(MVMCallsiteEntry));
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
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s; %s)",
                REPR(code)->name, MVM_6model_get_debug_name(tc, code));
        }
        code = find_invokee_internal(tc, code, tweak_cs, is);
    }
    return code;
}

MVM_USED_BY_JIT
MVMObject * MVM_frame_find_invokee_multi_ok(MVMThreadContext *tc, MVMObject *code,
                                            MVMCallsite **tweak_cs, MVMRegister *args,
                                            MVMuint16 *was_multi) {
    if (!code)
        MVM_exception_throw_adhoc(tc, "Cannot invoke null object");
    if (STABLE(code)->invoke == MVM_6model_invoke_default) {
        MVMInvocationSpec *is = STABLE(code)->invocation_spec;
        if (!is) {
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s; %s)", REPR(code)->name, MVM_6model_get_debug_name(tc, code));
        }
        if (is->md_cache_offset && is->md_valid_offset) {
            if (!IS_CONCRETE(code))
                MVM_exception_throw_adhoc(tc, "Can not invoke a code type object");
            if (MVM_p6opaque_read_int64(tc, code, is->md_valid_offset)) {
                MVMObject *md_cache = MVM_p6opaque_read_object(tc, code, is->md_cache_offset);
                if (was_multi)
                    *was_multi = 1;
                if (!MVM_is_null(tc, md_cache)) {
                    MVMObject *result = MVM_multi_cache_find_callsite_args(tc,
                        md_cache, *tweak_cs, args);
                    if (result)
                        return MVM_frame_find_invokee(tc, result, tweak_cs);
                }
            }
        }
        else if (!MVM_is_null(tc, is->md_class_handle)) {
            /* We might be able to dig straight into the multi cache and not
             * have to invoke the proto. Also on this path set up the offsets
             * so we can be faster in the future. */
            MVMRegister dest;
            if (!IS_CONCRETE(code))
                MVM_exception_throw_adhoc(tc, "Can not invoke a code type object");
            if (code->st->REPR->ID == MVM_REPR_ID_P6opaque) {
                is->md_valid_offset = MVM_p6opaque_attr_offset(tc, code->st->WHAT,
                    is->md_class_handle, is->md_valid_attr_name);
                is->md_cache_offset = MVM_p6opaque_attr_offset(tc, code->st->WHAT,
                    is->md_class_handle, is->md_cache_attr_name);
            }
            REPR(code)->attr_funcs.get_attribute(tc,
                STABLE(code), code, OBJECT_BODY(code),
                is->md_class_handle, is->md_valid_attr_name,
                is->md_valid_hint, &dest, MVM_reg_int64);
            if (dest.i64) {
                if (was_multi)
                    *was_multi = 1;
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

/* Rapid resolution of an invokee. Used by the specialized resolve code op. */
MVMObject * MVM_frame_resolve_invokee_spesh(MVMThreadContext *tc, MVMObject *invokee) {
    if (REPR(invokee)->ID == MVM_REPR_ID_MVMCode) {
        return invokee;
    }
    else {
        MVMInvocationSpec *is = STABLE(invokee)->invocation_spec;
        if (MVM_LIKELY(is && is->code_ref_offset && IS_CONCRETE(invokee)))
            return MVM_p6opaque_read_object(tc, invokee, is->code_ref_offset);
    }
    return tc->instance->VMNull;
}

/* Creates a MVMContent wrapper object around an MVMFrame. */
MVMObject * MVM_frame_context_wrapper(MVMThreadContext *tc, MVMFrame *f) {
    MVMObject *ctx;
    f = MVM_frame_force_to_heap(tc, f);
    MVMROOT(tc, f, {
        ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
        MVM_ASSIGN_REF(tc, &(ctx->header), ((MVMContext *)ctx)->body.context, f);
    });
    return ctx;
}

/* Gets, allocating if needed, the frame extra data structure for the given
 * frame. This is used to hold data that only a handful of frames need. */
MVMFrameExtra * MVM_frame_extra(MVMThreadContext *tc, MVMFrame *f) {
    if (!f->extra)
        f->extra = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, sizeof(MVMFrameExtra));
    return f->extra;
}

/* Set up special return data on a frame. */
void MVM_frame_special_return(MVMThreadContext *tc, MVMFrame *f,
                               MVMSpecialReturn special_return,
                               MVMSpecialReturn special_unwind,
                               void *special_return_data,
                               MVMSpecialReturnDataMark mark_special_return_data) {
    MVMFrameExtra *e = MVM_frame_extra(tc, f);
    e->special_return = special_return;
    e->special_unwind = special_unwind;
    e->special_return_data = special_return_data;
    e->mark_special_return_data = mark_special_return_data;
}

/* Clears any special return data on a frame. */
void MVM_frame_clear_special_return(MVMThreadContext *tc, MVMFrame *f) {
    if (f->extra) {
        f->extra->special_return = NULL;
        f->extra->special_unwind = NULL;
        f->extra->special_return_data = NULL;
        f->extra->mark_special_return_data = NULL;
    }
}
