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
 * bytecode. Assumes we are holding the CU's deserialize frame mutex (at
 * the time of writing, this is only called from instrumentation_level_barrier). */
static void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    MVMStaticFrameBody *static_frame_body = &static_frame->body;
    MVMCompUnit        *cu                = static_frame_body->cu;

    /* Ensure the frame is fully deserialized. */
    if (!static_frame_body->fully_deserialized) {
        MVMROOT(tc, static_frame, {
            MVM_bytecode_finish_frame(tc, cu, static_frame, 0);
        });
    }

    /* If we never invoked this compilation unit before, and we have spesh
     * enabled, we might either have no spesh log or a nearly full one. This
     * will cause problems with gathering data to OSR hot loops. */
    if (!cu->body.invoked) {
        cu->body.invoked = 1;
        if (tc->instance->spesh_enabled)
            MVM_spesh_log_new_compunit(tc);
    }

    /* Work size is number of locals/registers plus size of the maximum
     * call site argument list. */
    static_frame_body->work_size = sizeof(MVMRegister) * static_frame_body->num_locals;

    /* Validate the bytecode. */
    MVMROOT(tc, static_frame, {
        MVM_validate_static_frame(tc, static_frame);
    });

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
     * also because then it can be assigned into the gen2 static frame
     * without causing it to become an inter-gen root. */
    MVM_gc_allocate_gen2_default_set(tc);
    MVM_ASSIGN_REF(tc, &(static_frame->common.header), static_frame_body->spesh, /* no GC error */
        MVM_repr_alloc_init(tc, tc->instance->StaticFrameSpesh));
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* When we don't match the current instrumentation level, we hit this. It may
 * simply be that we never invoked the frame, in which case we prepare and
 * verify it. It may also be because we need to instrument the code for
 * profiling. */
static void instrumentation_level_barrier(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    MVMCompUnit *cu = static_frame->body.cu;
    MVMROOT2(tc, static_frame, cu, {
        /* Obtain mutex, so we don't end up with instrumentation races. */
        MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)cu->body.deserialize_frame_mutex);

        /* Re-check instrumentation level in case of races. */
        if (static_frame->body.instrumentation_level != tc->instance->instrumentation_level) {
            /* Prepare and verify if needed. */
            if (static_frame->body.instrumentation_level == 0)
                prepare_and_verify_static_frame(tc, static_frame);

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

            /* Set up inline cache for the frame. */
            MVM_disp_inline_cache_setup(tc, static_frame);

            /* Mark frame as being at the current instrumentation level. */
            MVM_barrier();
            static_frame->body.instrumentation_level = tc->instance->instrumentation_level;
        }

        /* Release the lock. */
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.deserialize_frame_mutex);
    });
}

/* Called when the GC destroys a frame. Since the frame may have been alive as
 * part of a continuation that was taken but never invoked, we should check
 * things normally cleaned up on return don't need cleaning up also. */
void MVM_frame_destroy(MVMThreadContext *tc, MVMFrame *frame) {
    MVM_args_proc_cleanup(tc, &frame->params);
    if (frame->env && !MVM_FRAME_IS_ON_CALLSTACK(tc, frame))
        MVM_free(frame->env);
    if (frame->extra) {
        MVMFrameExtra *e = frame->extra;
        MVM_free(e);
    }
}

/* Creates a frame for usage as a context only, possibly forcing all of the
 * static lexicals to be deserialized if it's used for auto-close purposes.
 * Since we're not creating it to run bytecode, just for the purpose of a
 * serialized closure, we don't create any call stack record for it. */
static MVMFrame * create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref, MVMint32 autoclose) {
    MVMFrame *frame;

    MVMROOT2(tc, static_frame, code_ref, {
        /* Ensure the frame is fully deserialized. */
        if (!static_frame->body.fully_deserialized) {
            MVM_reentrantmutex_lock(tc,
                (MVMReentrantMutex *)static_frame->body.cu->body.deserialize_frame_mutex);
            if (!static_frame->body.fully_deserialized)
                MVM_bytecode_finish_frame(tc, static_frame->body.cu, static_frame, 0);
            MVM_reentrantmutex_unlock(tc,
                (MVMReentrantMutex *)static_frame->body.cu->body.deserialize_frame_mutex);
        }

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
        frame->env = MVM_calloc(1, static_frame->body.env_size);
        frame->allocd_env = static_frame->body.env_size;
        if (autoclose) {
            MVMROOT2(tc, frame, static_frame, {
                MVMuint16 i;
                MVMuint16 num_lexicals = static_frame->body.num_lexicals;
                for (i = 0; i < num_lexicals; i++) {
                    if (!static_frame->body.static_env[i].o && static_frame->body.static_env_flags[i] == 1) {
                        MVMuint32 scid;
                        MVMuint32 objid;
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

/* Obtains memory for a frame that we are about to enter and run bytecode in. Prefers
 * the callstack by default, but can put the frame onto the heap if it tends to be
 * promoted there anyway. Returns a pointer to the frame wherever in memory it ends
 * up living. Separate versions for specialized and unspecialized frames. */
static MVMFrame * allocate_unspecialized_frame(MVMThreadContext *tc,
        MVMStaticFrame *static_frame, MVMint32 heap) {
    MVMFrame *frame;
    MVMint32 work_size = static_frame->body.work_size;
    MVMint32 env_size = static_frame->body.env_size;
    if (heap) {
        /* Allocate frame on the heap. The callstack record includes space
         * for the work registers and ->work will have been set up already. */
        MVMROOT(tc, static_frame, {
            if (tc->cur_frame)
                MVM_frame_force_to_heap(tc, tc->cur_frame);
            frame = MVM_callstack_allocate_heap_frame(tc, work_size)->frame;
        });

        /* If we have an environment, that needs allocating separately for
         * heap-based frames. */
        if (env_size) {
            frame->env = MVM_calloc(1, env_size);
            frame->allocd_env = env_size;
        }
    }
    else {
        /* Allocate the frame on the call stack. The callstack record includes
         * space for both the work registers and the environment, and both the
         * ->work and ->env pointers will have been set up already, but we do
         *  need to clear the environment. */
        MVMCallStackFrame *record = MVM_callstack_allocate_frame(tc, work_size, env_size);
        frame = &(record->frame);
        memset(frame->env, 0, env_size);
    }

    /* Copy frame template with VMNulls in to place. */
    memcpy(frame->work, static_frame->body.work_initial,
        sizeof(MVMRegister) * static_frame->body.num_locals);

    /* Set static frame and caller before we let this frame escape and the GC
     * see it. */
    frame->static_info = static_frame;
    frame->caller = tc->cur_frame;

    return frame;
}
static MVMFrame * allocate_specialized_frame(MVMThreadContext *tc,
        MVMStaticFrame *static_frame, MVMSpeshCandidate *spesh_cand, MVMint32 heap) {
    MVMFrame *frame;
    MVMint32 work_size = spesh_cand->body.work_size;
    MVMint32 env_size = spesh_cand->body.env_size;
    if (heap) {
        /* Allocate frame on the heap. The callstack record includes space
         * for the work registers and ->work will have been set up already. */
        MVMROOT2(tc, static_frame, spesh_cand, {
            if (tc->cur_frame)
                MVM_frame_force_to_heap(tc, tc->cur_frame);
            frame = MVM_callstack_allocate_heap_frame(tc, work_size)->frame;
        });

        /* Zero out the work memory. Spesh makes sure we have VMNull setup in
         * the places we need it. */
        memset(frame->work, 0, work_size);

        /* If we have an environment, that needs allocating separately for
         * heap-based frames. */
        if (env_size) {
            frame->env = MVM_calloc(1, env_size);
            frame->allocd_env = env_size;
        }
    }
    else {
        /* Allocate the frame on the call stack. The callstack record includes
         * space for both the work registers and the environment, and both the
         * ->work and ->env pointers will have been set up already. We need to
         * zero out the work and env; we rely on them being contiguous and so
         * zero them with a single memset call. */
        MVMCallStackFrame *record = MVM_callstack_allocate_frame(tc, work_size, env_size);
        frame = &(record->frame);
        memset(frame->work, 0, work_size + env_size);
    }

    /* Set static frame and caller before we let this frame escape and the GC
     * see it. */
    frame->static_info = static_frame;
    frame->caller = tc->cur_frame;

    return frame;
}

/* Set up a deopt frame. */
void MVM_frame_setup_deopt(MVMThreadContext *tc, MVMFrame *frame, MVMStaticFrame *static_frame,
        MVMCode *code_ref) {
    /* Initialize various frame properties. */
    frame->static_info = static_frame;
    frame->code_ref = (MVMObject *)code_ref;
    frame->outer = code_ref->body.outer;
    frame->spesh_cand = NULL;
    frame->spesh_correlation_id = 0;
}

/* Sets up storage for state variables. We do this after tc->cur_frame became
 * the current frame, to make sure these new objects will certainly get marked
 * if GC is triggered along the way. */
static void setup_state_vars(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    /* Drag everything out of static_frame_body before we start,
     * as GC action may invalidate it. */
    MVMFrame    *frame     = tc->cur_frame;
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

/* Produces an error on outer frame mis-match. */
static void report_outer_conflict(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMFrame *outer) {
    char *frame_cuuid = MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid);
    char *frame_name;
    char *outer_cuuid = MVM_string_utf8_encode_C_string(tc, outer->static_info->body.cuuid);
    char *outer_name;
    char *frame_outer_cuuid = MVM_string_utf8_encode_C_string(tc,
            static_frame->body.outer
                ? static_frame->body.outer->body.cuuid
                : tc->instance->str_consts.empty);
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

    if (static_frame->body.outer && static_frame->body.outer->body.name) {
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

/* Dispatches execution to the specified code object with the specified args. */
void MVM_frame_dispatch(MVMThreadContext *tc, MVMCode *code, MVMArgs args, MVMint32 spesh_cand) {
    /* Did we get given a specialization? */
    MVMStaticFrame *static_frame = code->body.sf;
    MVMStaticFrameSpesh *spesh;
    if (spesh_cand < 0) {
        /* No. In that case it's possible we never even invoked this frame
         * before, or never at the current instrumentation level; check and
         * handle this situation if so. */
        if (MVM_UNLIKELY(static_frame->body.instrumentation_level != tc->instance->instrumentation_level)) {
            MVMROOT2(tc, static_frame, code, {
                instrumentation_level_barrier(tc, static_frame);
            });
        }

        /* Run the specialization argument guard to see if we can use one. */
        spesh = static_frame->body.spesh;
        spesh_cand = MVM_spesh_arg_guard_run(tc, spesh->body.spesh_arg_guard,
            args, NULL);
    }
    else {
        spesh = static_frame->body.spesh;
#if MVM_SPESH_CHECK_PRESELECTION
        MVMint32 certain = -1;
        MVMint32 correct = MVM_spesh_arg_guard_run(tc, spesh->body.spesh_arg_guard,
            args, &certain);
        if (spesh_cand != correct && spesh_cand != certain) {
            fprintf(stderr, "Inconsistent spesh preselection of '%s' (%s): got %d, not %d\n",
                MVM_string_utf8_encode_C_string(tc, static_frame->body.name),
                MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid),
                spesh_cand, correct);
            MVM_dump_backtrace(tc);
        }
#endif
    }

    /* Ensure we have an outer if needed. This is done ahead of allocating the
     * new frame, since an autoclose will force the callstack on to the heap. */
    MVMFrame *outer = code->body.outer;
    if (outer) {
        /* We were provided with an outer frame. Ensure that it is based on the
         * correct static frame (compare on bytecode address to cope with
         * nqp::freshcoderef). */
        if (MVM_UNLIKELY(static_frame->body.outer == 0 || outer->static_info->body.orig_bytecode != static_frame->body.outer->body.orig_bytecode))
            report_outer_conflict(tc, static_frame, outer);
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
            MVMROOT3(tc, static_frame, code, static_code, {
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

    /* Now go by whether we have a specialization. */
    MVMFrame *frame;
    MVMuint8 *chosen_bytecode;
    if (spesh_cand >= 0) {
        MVMSpeshCandidate *chosen_cand = spesh->body.spesh_candidates[spesh_cand];
        if (static_frame->body.allocate_on_heap) {
            MVMROOT4(tc, static_frame, code, outer, chosen_cand, {
                frame = allocate_specialized_frame(tc, static_frame, chosen_cand, 1);
            });
        }
        else {
            frame = allocate_specialized_frame(tc, static_frame, chosen_cand, 0);
            frame->spesh_correlation_id = 0;
        }
        frame->code_ref = (MVMObject *)code;
        frame->outer = outer;
        if (chosen_cand->body.jitcode) {
            chosen_bytecode = chosen_cand->body.jitcode->bytecode;
            frame->jit_entry_label = chosen_cand->body.jitcode->labels[0];
        }
        else {
            chosen_bytecode = chosen_cand->body.bytecode;
        }
        frame->effective_spesh_slots = chosen_cand->body.spesh_slots;
        frame->spesh_cand = chosen_cand;

        /* Initialize argument processing. */
        MVM_args_proc_setup(tc, &(frame->params), args);
    }
    else {
        MVMint32 on_heap = static_frame->body.allocate_on_heap;
        if (on_heap) {
            MVMROOT3(tc, static_frame, code, outer, {
                frame = allocate_unspecialized_frame(tc, static_frame, 1);
            });
        }
        else {
            frame = allocate_unspecialized_frame(tc, static_frame, 0);
            frame->spesh_cand = NULL;
            frame->effective_spesh_slots = NULL;
            frame->spesh_correlation_id = 0;
        }
        frame->code_ref = (MVMObject *)code;
        frame->outer = outer;
        chosen_bytecode = static_frame->body.bytecode;

        /* Initialize argument processing. Do this before the GC might process the frame. */
        MVM_args_proc_setup(tc, &(frame->params), args);

        /* If we should be spesh logging, set the correlation ID. */
        if (tc->instance->spesh_enabled && tc->spesh_log && static_frame->body.bytecode_size < MVM_SPESH_MAX_BYTECODE_SIZE) {
            if (spesh->body.spesh_entries_recorded++ < MVM_SPESH_LOG_LOGGED_ENOUGH) {
                MVMint32 id = ++tc->spesh_cid;
                frame->spesh_correlation_id = id;
                MVMROOT3(tc, static_frame, code, outer, {
                    if (on_heap) {
                        MVMROOT(tc, frame, {
                            MVM_spesh_log_entry(tc, id, static_frame, args);
                        });
                    }
                    else {
                        MVMROOT2(tc, frame->caller, frame->static_info, {
                            MVM_spesh_log_entry(tc, id, static_frame, args);
                        });
                    }
                });
            }
        }
    }

    MVM_jit_code_trampoline(tc);

    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    *(tc->interp_cur_op) = chosen_bytecode;
    *(tc->interp_bytecode_start) = chosen_bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame->body.cu;

    if (static_frame->body.has_state_vars)
        setup_state_vars(tc, static_frame);
}

/* Dispatches to a frame with zero args. Convenience for various entrypoint
 * style locations. */
void MVM_frame_dispatch_zero_args(MVMThreadContext *tc, MVMCode *code) {
    MVMArgs args = {
        .callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_ZERO_ARITY),
        .source = NULL,
        .map = NULL
    };
    MVM_frame_dispatch(tc, code, args, -1);
}

/* Dispatches to a frame with args set up by C code. Also sets the expected
 * return type and destination for the return value. */
void MVM_frame_dispatch_from_c(MVMThreadContext *tc, MVMCode *code,
        MVMCallStackArgsFromC *args_record, MVMRegister *return_value,
        MVMReturnType return_type) {
    MVMFrame *cur_frame = tc->cur_frame;
    cur_frame->return_value = return_value;
    cur_frame->return_type = return_type;
    cur_frame->return_address = *(tc->interp_cur_op);
    MVM_frame_dispatch(tc, code, args_record->args, -1);
}

/* Moves the specified frame from the stack and on to the heap. Must only
 * be called if the frame is not already there. Use MVM_frame_force_to_heap
 * when not sure. */
MVMFrame * MVM_frame_move_to_heap(MVMThreadContext *tc, MVMFrame *frame) {
    /* To keep things simple, we'll promote all non-promoted frames on the call
     * stack. We walk the call stack to find them. */
    MVMFrame *cur_to_promote = NULL;
    MVMFrame *new_cur_frame = NULL;
    MVMFrame *update_caller = NULL;
    MVMFrame *result = NULL;
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_init(tc, &iter, tc->stack_top);
    MVM_CHECK_CALLER_CHAIN(tc, cur_to_promote);
    MVMROOT4(tc, new_cur_frame, update_caller, cur_to_promote, result, {
        while (MVM_callstack_iter_move_next(tc, &iter)) {
            /* Check this isn't already a heap or promoted frame; if it is, we're
             * done. */
            MVMCallStackRecord *record = MVM_callstack_iter_current(tc, &iter);
            if (MVM_callstack_kind_ignoring_deopt(record) != MVM_CALLSTACK_RECORD_FRAME)
                break;
            MVMCallStackFrame *unpromoted_record = (MVMCallStackFrame *)record;
            cur_to_promote = &(unpromoted_record->frame);

            /* Move any lexical environment to the heap, as it may now
             * out-live the callstack entry. */
            MVMuint32 env_size = cur_to_promote->allocd_env;
            if (env_size) {
                MVMRegister *heap_env = MVM_malloc(env_size);
                memcpy(heap_env, cur_to_promote->env, env_size);
                cur_to_promote->env = heap_env;
            }
            else {
                /* Stack frames may set up the env pointer even if it's to
                 * an empty area (avoids branches); ensure it is nulled out
                 * so we don't try to do a bogus free later. */
                cur_to_promote->env = NULL;
            }

            /* Clear any dynamic lexical cache entry, as it may point into an
             * environment that gets moved to the heap. */
            MVMFrameExtra *e = cur_to_promote->extra;
            if (e)
                e->dynlex_cache_name = NULL;

            /* Allocate a heap frame. */
            /* frame is safe from the GC as we wouldn't be here if it wasn't on the stack */
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

            /* Update stack record to indicate the promotion, and make it
             * reference the heap frame. */
            if (record->kind == MVM_CALLSTACK_RECORD_DEOPT_FRAME)
                record->orig_kind = MVM_CALLSTACK_RECORD_PROMOTED_FRAME;
            else
                record->kind = MVM_CALLSTACK_RECORD_PROMOTED_FRAME;
            ((MVMCallStackPromotedFrame *)record)->frame = promoted;

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
                if (MVM_FRAME_IS_ON_CALLSTACK(tc, cur_to_promote->caller)) {
                    /* Clear caller in promoted frame, to avoid a heap -> stack
                     * reference if we GC during this loop. */
                    promoted->caller = NULL;
                    update_caller = promoted;
                }
                else {
                    if (cur_to_promote == tc->thread_entry_frame)
                        tc->thread_entry_frame = promoted;
                    MVM_gc_write_barrier(tc, (MVMCollectable*)promoted, (MVMCollectable*)promoted->caller);
                }
            }
            else {
                /* End of caller chain; check if we promoted the entry
                 * frame */
                if (cur_to_promote == tc->thread_entry_frame)
                    tc->thread_entry_frame = promoted;
            }
        }
    });
    MVM_CHECK_CALLER_CHAIN(tc, new_cur_frame);

    /* All is promoted. Update thread's current frame pointer. */
    tc->cur_frame = new_cur_frame;

    /* Hand back new location of promoted frame. */
    if (!result)
        MVM_panic(1, "Failed to find frame to promote on call stack");
    return result;
}

/* This function is to be used by the debugserver if a thread is currently
 * blocked. */
MVMFrame * MVM_frame_debugserver_move_to_heap(MVMThreadContext *debug_tc,
        MVMThreadContext *owner, MVMFrame *frame) {
    /* To keep things simple, we'll promote all non-promoted frames on the call
     * stack. We walk the call stack to find them. */
    MVMFrame *cur_to_promote = NULL;
    MVMFrame *new_cur_frame = NULL;
    MVMFrame *update_caller = NULL;
    MVMFrame *result = NULL;
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_init(owner, &iter, owner->stack_top);
    MVM_CHECK_CALLER_CHAIN(owner, cur_to_promote);
    MVMROOT4(debug_tc, new_cur_frame, update_caller, cur_to_promote, result, {
        while (MVM_callstack_iter_move_next(owner, &iter)) {
            /* Check this isn't already a heap or promoted frame; if it is, we're
             * done. */
            MVMCallStackRecord *record = MVM_callstack_iter_current(owner, &iter);
            if (MVM_callstack_kind_ignoring_deopt(record) != MVM_CALLSTACK_RECORD_FRAME)
                break;
            MVMCallStackFrame *unpromoted_record = (MVMCallStackFrame *)record;
            cur_to_promote = &(unpromoted_record->frame);

            /* Allocate a heap frame. */
            /* frame is safe from the GC as we wouldn't be here if it wasn't on the stack */
            MVMFrame *promoted = MVM_gc_allocate_frame(debug_tc);

            /* Copy current frame's body to it. */
            memcpy(
                (char *)promoted + sizeof(MVMCollectable),
                (char *)cur_to_promote + sizeof(MVMCollectable),
                sizeof(MVMFrame) - sizeof(MVMCollectable));

            /* Update stack record to indicate the promotion, and make it
             * reference the heap frame. */
            if (record->kind == MVM_CALLSTACK_RECORD_DEOPT_FRAME)
                record->orig_kind = MVM_CALLSTACK_RECORD_PROMOTED_FRAME;
            else
                record->kind = MVM_CALLSTACK_RECORD_PROMOTED_FRAME;
            ((MVMCallStackPromotedFrame *)record)->frame = promoted;

            /* Update caller of previously promoted frame, if any. This is the
             * only reference that might point to a non-heap frame. */
            if (update_caller) {
                MVM_ASSIGN_REF(debug_tc, &(update_caller->header),
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
                if (MVM_FRAME_IS_ON_CALLSTACK(owner, cur_to_promote->caller)) {
                    /* Clear caller in promoted frame, to avoid a heap -> stack
                     * reference if we GC during this loop. */
                    promoted->caller = NULL;
                    update_caller = promoted;
                }
                else {
                    if (cur_to_promote == owner->thread_entry_frame)
                        owner->thread_entry_frame = promoted;
                    MVM_gc_write_barrier(debug_tc, (MVMCollectable*)promoted, (MVMCollectable*)promoted->caller);
                }
            }
            else {
                /* End of caller chain; check if we promoted the entry
                 * frame */
                if (cur_to_promote == owner->thread_entry_frame)
                    owner->thread_entry_frame = promoted;
            }
        }
    });
    MVM_CHECK_CALLER_CHAIN(owner, new_cur_frame);

    /* All is promoted. Update thread's current frame pointer. */
    owner->cur_frame = new_cur_frame;

    /* Hand back new location of promoted frame. */
    if (!result)
        MVM_panic(1, "Failed to find frame to promote on foreign thread's call stack");
    return result;
}

/* Attempt to return from the current frame. Returns non-zero if we can,
 * and zero if there is nowhere to return to (which would signal the exit
 * of the interpreter). */
static void remove_after_handler(MVMThreadContext *tc, void *sr_data) {
    MVM_callstack_unwind_frame(tc, 0);
}
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;

    if (cur_frame->static_info->body.has_exit_handler &&
            !(cur_frame->flags & MVM_FRAME_FLAG_EXIT_HAND_RUN)) {
        /* Set us up to run exit handler, and make it so we'll really exit the
         * frame when that has been done. */
        if (tc->cur_frame == tc->thread_entry_frame)
            MVM_exception_throw_adhoc(tc, "Thread entry point frame cannot have an exit handler");
        MVMFrame *caller = cur_frame->caller;
        if (!caller)
            MVM_exception_throw_adhoc(tc, "Entry point frame cannot have an exit handler");

        MVMHLLConfig *hll = MVM_hll_current(tc);
        MVMObject *result;
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
                    case MVM_RETURN_UINT:
                        result = MVM_repr_box_uint(tc, hll->int_box_type, caller->return_value->u64);
                        break;
                    case MVM_RETURN_NUM:
                        result = MVM_repr_box_num(tc, hll->num_box_type, caller->return_value->n64);
                        break;
                    case MVM_RETURN_STR:
                        result = MVM_repr_box_str(tc, hll->str_box_type, caller->return_value->s);
                        break;
                    case MVM_RETURN_VOID:
                        result = cur_frame->extra && cur_frame->extra->exit_handler_result
                            ? cur_frame->extra->exit_handler_result
                            : tc->instance->VMNull;
                        break;
                    default:
                        result = tc->instance->VMNull;
                }
            });
        }

        cur_frame->flags |= MVM_FRAME_FLAG_EXIT_HAND_RUN;
        MVM_callstack_allocate_special_return(tc, remove_after_handler, NULL, NULL, 0);
        MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ));
        args_record->args.source[0].o = cur_frame->code_ref;
        args_record->args.source[1].o = result;
        MVM_frame_dispatch_from_c(tc, hll->exit_handler, args_record, NULL, MVM_RETURN_VOID);
        return 1;
    }
    else {
        /* No exit handler, so a straight return. */
        return MVM_callstack_unwind_frame(tc, 0);
    }
}

/* Try a return from the current frame; skip running any exit handlers. */
MVMuint64 MVM_frame_try_return_no_exit_handlers(MVMThreadContext *tc) {
    return MVM_callstack_unwind_frame(tc, 0);
}

/* Unwinds execution state to the specified frame, placing control flow at either
 * an absolute or relative (to start of target frame) address and optionally
 * setting a returned result. */
typedef struct {
    MVMFrame  *frame;
    MVMuint8  *abs_addr;
    MVMuint32  rel_addr;
    void      *jit_return_label;
    MVMuint8   exceptional;
} MVMUnwindData;
static void mark_unwind_data(MVMThreadContext *tc, void *sr_data, MVMGCWorklist *worklist) {
    MVMUnwindData *ud  = (MVMUnwindData *)sr_data;
    MVM_gc_worklist_add(tc, worklist, &(ud->frame));
}
static void continue_unwind(MVMThreadContext *tc, void *sr_data) {
    MVMUnwindData *ud  = (MVMUnwindData *)sr_data;
    MVMFrame *frame    = ud->frame;
    MVMuint8 *abs_addr = ud->abs_addr;
    MVMuint32 rel_addr = ud->rel_addr;
    MVMuint32 ex       = ud->exceptional;
    void *jit_return_label = ud->jit_return_label;
    MVM_frame_unwind_to(tc, frame, abs_addr, rel_addr, NULL, jit_return_label, ex);
}

MVMint8 MVM_frame_continue_conflicting_unwind(MVMThreadContext *tc, MVMFrame *up_to,
        MVMuint8 exceptional) {
    /* A conflicting unwind is one where the unwind continuation data is higher
     * in the call stack than the other unwinds target frame (i.e. one unwind
     * wound skip over the continuation data of the other). If that's the case
     * we need to find out which unwind would unwind more of the stack, that's
     * the winning unwind that should continue. */
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_or_special_init(tc, &iter, tc->stack_top);
    MVMUnwindData *data = 0;

    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *record = MVM_callstack_iter_current(tc, &iter);
        if (!data) {
            if (record->kind == MVM_CALLSTACK_RECORD_SPECIAL_RETURN) {
                data = MVM_callstack_get_special_return_data(tc,
                        record, &continue_unwind);
                if (data && data->exceptional != exceptional) {
                    // Not a conflicting unwind.
                    return 0;
                }
            }
            else if (MVM_callstack_iter_current_frame(tc, &iter) == up_to) {
                // No conflicting unwind found.
                return 0;
            }
        }
        else {
            MVMFrame *f = MVM_callstack_iter_current_frame(tc, &iter);
            if (f == up_to) {
                /* We've seen the other unwind target first, so ours will
                 * unwind more. */
                continue_unwind(tc, data);
                return 1;
            }
            else if (f == data->frame) {
                /* We've seen our unwind's target first, so the other will
                 * unwind more. */
                return 0;
            }
        }
    }
    MVM_panic(1, "Did not find expected unwind target frame.");
}

void MVM_frame_unwind_to(MVMThreadContext *tc, MVMFrame *frame, MVMuint8 *abs_addr,
                         MVMuint32 rel_addr, MVMObject *return_value,
                         void *jit_return_label, MVMuint8 exceptional) {
    /* Lazy deopt means that we might have located an exception handler in
     * optimized code, but then at the point we call MVM_callstack_unwind_frame we'll
     * end up deoptimizing it. That means the address here will be out of date.
     * This can only happen if we actually have frames to unwind; if we are
     * already in the current frame it cannot. So first handle that local
     * jump case... */
    if (tc->cur_frame == frame) {
        if (abs_addr)
            *tc->interp_cur_op = abs_addr;
        else if (rel_addr)
            *tc->interp_cur_op = *tc->interp_bytecode_start + rel_addr;
        if (jit_return_label)
            MVM_jit_code_set_current_position(tc, tc->cur_frame->spesh_cand->body.jitcode,
                    tc->cur_frame, jit_return_label);
    }

    /* Failing that, we'll set things up as if we're doing a return into the
     * frame, thus tweaking its return address and JIT label. That will cause
     * MVM_callstack_unwind_frame and any lazy deopt to move use to the right place. */
    else {
        while (tc->cur_frame != frame) {
            MVMFrame *cur_frame = tc->cur_frame;

            if (cur_frame->static_info->body.has_exit_handler &&
                    !(cur_frame->flags & MVM_FRAME_FLAG_EXIT_HAND_RUN)) {
                /* We're unwinding a frame with an exit handler. Thus we need to
                 * pause the unwind, run the exit handler, and keep enough info
                 * around in order to finish up the unwind afterwards. */
                if (return_value)
                    MVM_exception_throw_adhoc(tc, "return_value + exit_handler case NYI");

                /* Force the frame onto the heap, since we'll reference it from the
                 * unwind data. */
                MVMROOT3(tc, frame, cur_frame, return_value, {
                    frame = MVM_frame_force_to_heap(tc, frame);
                    cur_frame = tc->cur_frame;
                });

                MVMFrame *caller = cur_frame->caller;
                if (!caller)
                    MVM_exception_throw_adhoc(tc, "Entry point frame cannot have an exit handler");
                if (cur_frame == tc->thread_entry_frame)
                    MVM_exception_throw_adhoc(tc, "Thread entry point frame cannot have an exit handler");

                MVMHLLConfig *hll = MVM_hll_current(tc);
                MVMUnwindData *ud = MVM_callstack_allocate_special_return(tc,
                        continue_unwind, NULL, mark_unwind_data, sizeof(MVMUnwindData));
                ud->frame = frame;
                ud->abs_addr = abs_addr;
                ud->rel_addr = rel_addr;
                ud->jit_return_label = jit_return_label;
                ud->exceptional = exceptional;
                cur_frame->flags |= MVM_FRAME_FLAG_EXIT_HAND_RUN;
                MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                        MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ));
                args_record->args.source[0].o = cur_frame->code_ref;
                args_record->args.source[1].o = tc->instance->VMNull;
                MVM_frame_dispatch_from_c(tc, hll->exit_handler, args_record, NULL,
                        MVM_RETURN_VOID);
                return;
            }
            else {
                /* If we're profiling, log an exit. */
                if (tc->instance->profiling)
                    MVM_profile_log_unwind(tc);

                /* No exit handler, so just remove the frame, first tweaking
                 * the return address so we can have the interpreter moved to
                 * the right place. */
                MVMFrame *caller = cur_frame->caller;
                if (caller == frame) {
                    if (abs_addr)
                        caller->return_address = abs_addr;
                    else if (rel_addr)
                        caller->return_address = MVM_frame_effective_bytecode(caller) + rel_addr; 
                    if (jit_return_label)
                        caller->jit_entry_label = jit_return_label;
                }
                if (MVM_FRAME_IS_ON_CALLSTACK(tc, frame)) {
                    MVMROOT(tc, return_value, {
                        if (!MVM_callstack_unwind_frame(tc, 1))
                            MVM_panic(1, "Internal error: Unwound entire stack and missed handler");
                    });
                }
                else {
                    MVMROOT2(tc, return_value, frame, {
                        if (!MVM_callstack_unwind_frame(tc, 1))
                            MVM_panic(1, "Internal error: Unwound entire stack and missed handler");
                    });
                }
            }
        }
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
    return code->body.code_object
        ? code->body.code_object
        : tc->instance->VMNull;
}

/* Given the specified code object, sets its outer to the current scope. */
void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code) {
    MVMFrame *captured;
    if (MVM_UNLIKELY(REPR(code)->ID != MVM_REPR_ID_MVMCode))
        MVM_exception_throw_adhoc(tc,
            "Can only perform capturelex on object with representation MVMCode");
    MVMROOT(tc, code, {
        captured = MVM_frame_force_to_heap(tc, tc->cur_frame);
    });
    MVM_ASSIGN_REF(tc, &(code->header), ((MVMCode*)code)->body.outer, captured);
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
 * static frame on the call stack, so that the QUIT will discover the correct
 * $x.
 */
void MVM_frame_capture_inner(MVMThreadContext *tc, MVMObject *code) {
    MVMFrame *outer;
    MVMROOT(tc, code, {
        MVMStaticFrame *sf_outer = ((MVMCode*)code)->body.sf->body.outer;
        MVMROOT(tc, sf_outer, {
            outer = create_context_only(tc, sf_outer, (MVMObject *)sf_outer->body.static_code, 1);
        });
        MVMROOT(tc, outer, {
            MVMFrame *outer_outer = autoclose(tc, sf_outer->body.outer);
            MVM_ASSIGN_REF(tc, &(outer->header), outer->outer, outer_outer);
        });
    });
    MVM_ASSIGN_REF(tc, &(code->header), ((MVMCode*)code)->body.outer, outer);
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
        MVMuint32 i;
        flags = NULL;
        for (i = 0; i < f->spesh_cand->body.num_inlines; i++) {
            MVMStaticFrame *isf = f->spesh_cand->body.inlines[i].sf;
            effective_idx = idx - f->spesh_cand->body.inlines[i].lexicals_start;
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
        MVMuint32 scid, objid;
        if (MVM_bytecode_find_static_lexical_scref(tc, effective_sf->body.cu,
                effective_sf, effective_idx, &scid, &objid)) {
            MVMSerializationContext *sc;
            MVMObject *resolved;
            /* XXX This really ought to go into the bytecode validator
             * instead for better performance and earlier crashing */
            if (effective_sf->body.cu->body.num_scs <= scid) {
                MVM_exception_throw_adhoc(tc,
                    "Bytecode corruption: illegal sc dependency of lexical: %d > %d", scid, effective_sf->body.cu->body.num_scs);
            }
            sc = MVM_sc_get_sc(tc, effective_sf->body.cu, scid);
            if (sc == NULL)
                MVM_exception_throw_adhoc(tc,
                    "SC not yet resolved; lookup failed");
            MVMROOT2(tc, f, effective_sf, {
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
int MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type, MVMRegister *r) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init_for_outers(tc, &fw, tc->cur_frame);
    MVMRegister *res = MVM_frame_lexical_lookup_using_frame_walker(tc, &fw, name, type);

    if (res == NULL) {
        MVMCode *resolver = tc->cur_frame->static_info->body.cu->body.resolver;
        if (resolver) {
            MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                    MVM_callsite_get_common(tc, MVM_CALLSITE_ID_STR));
            args_record->args.source[0].s = name;
            MVM_frame_dispatch_from_c(tc, resolver, args_record, r, MVM_RETURN_OBJ);
        }
        else if (MVM_UNLIKELY(type != MVM_reg_obj)) {
            char *c_name = MVM_string_utf8_encode_C_string(tc, name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste, "No lexical found with name '%s'",
                c_name);
        }
        return 0;
    }
    else {
        *r = *res;
        return 1;
    }
}

/* Binds the specified value to the given lexical, finding it along the static
 * chain. */
MVM_PUBLIC void MVM_frame_bind_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type, MVMRegister value) {
    MVMFrame *cur_frame = tc->cur_frame;
    while (cur_frame != NULL) {
        if (cur_frame->static_info->body.num_lexicals) {
            MVMuint32 idx = MVM_get_lexical_by_name(tc, cur_frame->static_info, name);
            if (idx != MVM_INDEX_HASH_NOT_FOUND) {
                if (cur_frame->static_info->body.lexical_types[idx] == type) {
                    if (type == MVM_reg_obj || type == MVM_reg_str) {
                        MVM_ASSIGN_REF(tc, &(cur_frame->header),
                            cur_frame->env[idx].o, value.o);
                    }
                    else {
                        cur_frame->env[idx] = value;
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
void MVM_frame_find_lexical_by_name_outer(MVMThreadContext *tc, MVMString *name, MVMRegister *result) {
    int found;
    MVMROOT(tc, name, {
        found = MVM_frame_find_lexical_by_name_rel(tc, name, tc->cur_frame->outer, result);
    });
    if (MVM_UNLIKELY(!found)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "No lexical found with name '%s'",
            c_name);
    }
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. Only works if it's an object lexical.  */
int MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame, MVMRegister *r) {
    while (cur_frame != NULL) {
        if (cur_frame->static_info->body.num_lexicals) {
            MVMuint32 idx = MVM_get_lexical_by_name(tc, cur_frame->static_info, name);
            if (idx != MVM_INDEX_HASH_NOT_FOUND) {
                if (cur_frame->static_info->body.lexical_types[idx] == MVM_reg_obj) {
                    MVMRegister *result = &cur_frame->env[idx];
                    if (!result->o)
                        MVM_frame_vivify_lexical(tc, cur_frame, idx);
                    *r = *result;
                    return 1;
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
    MVMCode *resolver = tc->cur_frame->static_info->body.cu->body.resolver;
    if (resolver) {
        MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                MVM_callsite_get_common(tc, MVM_CALLSITE_ID_STR));
        args_record->args.source[0].s = name;
        MVM_frame_dispatch_from_c(tc, resolver, args_record, r, MVM_RETURN_OBJ);
        return 1;
    }
    return 0;
}

/* Performs some kind of lexical lookup using the frame walker. The exact walk
 * that is done depends on the frame walker setup. */
MVMRegister * MVM_frame_lexical_lookup_using_frame_walker(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw, MVMString *name, MVMuint16 type) {
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(name));
    while (MVM_spesh_frame_walker_next(tc, fw)) {
        MVMRegister *found;
        MVMuint16 found_kind;
        if (MVM_spesh_frame_walker_get_lex(tc, fw, name, &found, &found_kind, 1, NULL)) {
            MVM_spesh_frame_walker_cleanup(tc, fw);
            MVM_gc_root_temp_pop(tc);
            if (found_kind == type) {
                return found;
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
    MVM_spesh_frame_walker_cleanup(tc, fw);
    MVM_gc_root_temp_pop(tc);
    return NULL;
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. It checks all outer frames of the caller frame chain.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel_caller(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_caller_frame) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init(tc, &fw, cur_caller_frame, 1);
    return MVM_frame_lexical_lookup_using_frame_walker(tc, &fw, name, MVM_reg_obj);
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
MVMRegister * MVM_frame_find_dynamic_using_frame_walker(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw, MVMString *name, MVMuint16 *type, MVMFrame *initial_frame,
        MVMint32 vivify, MVMFrame **found_frame) {
    FILE *dlog = tc->instance->dynvar_log_fh;
    MVMuint32 fcost = 0;  /* frames traversed */
    MVMuint32 icost = 0;  /* inlines traversed */
    MVMuint32 ecost = 0;  /* frames traversed with empty cache */
    MVMuint32 xcost = 0;  /* frames traversed with wrong name */
    MVMFrame *last_real_frame = initial_frame;
    char *c_name;
    MVMuint64 start_time;
    MVMuint64 last_time;

    if (MVM_UNLIKELY(!name))
        MVM_exception_throw_adhoc(tc, "Contextual name cannot be null");
    if (MVM_UNLIKELY(dlog)) {
        c_name = MVM_string_utf8_encode_C_string(tc, name);
        start_time = uv_hrtime();
        last_time = tc->instance->dynvar_log_lasttime;
    }

    /* Traverse with the frame walker. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&initial_frame);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&last_real_frame);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&name);
    while (MVM_spesh_frame_walker_next(tc, fw)) {
        MVMRegister *result;

        /* If we're not currently visiting an inline, then see if we've a
         * cache entry on this frame. Also track costs. */
        if (!MVM_spesh_frame_walker_is_inline(tc, fw)) {
            MVMFrameExtra *e;
            last_real_frame = MVM_spesh_frame_walker_current_frame(tc, fw);
            e = last_real_frame->extra;
            if (e && e->dynlex_cache_name) {
                if (MVM_string_equal(tc, name, e->dynlex_cache_name)) {
                    /* Matching cache entry; if it's far from us, try to cache
                     * it closer to us. */
                    MVMRegister *result = e->dynlex_cache_reg;
                    *type = e->dynlex_cache_type;
                    if (fcost+icost > 5)
                        try_cache_dynlex(tc, initial_frame, last_real_frame, name, result, *type, fcost, icost);
                    if (dlog) {
                        fprintf(dlog, "C %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                        fflush(dlog);
                        MVM_free(c_name);
                        tc->instance->dynvar_log_lasttime = uv_hrtime();
                    }
                    *found_frame = last_real_frame;
                    MVM_gc_root_temp_pop_n(tc, 3);
                    MVM_spesh_frame_walker_cleanup(tc, fw);
                    return result;
                }
                else
                    xcost++;
            }
            else
                ecost++;
            fcost++;
        }
        else
            icost++;

        /* See if we have the lexical at this location. */
        if (MVM_spesh_frame_walker_get_lex(tc, fw, name, &result, type, vivify, found_frame)) {
            /* Yes, found it. If we walked some way, try to cache it. */
            if (fcost+icost > 1)
                try_cache_dynlex(tc, initial_frame, last_real_frame, name,
                    result, *type, fcost, icost);
            if (dlog) {
                fprintf(dlog, "%s %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n",
                        MVM_spesh_frame_walker_is_inline(tc, fw) ? "I" : "F",
                        c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
                fflush(dlog);
                MVM_free(c_name);
                tc->instance->dynvar_log_lasttime = uv_hrtime();
            }
            MVM_gc_root_temp_pop_n(tc, 3);
            MVM_spesh_frame_walker_cleanup(tc, fw);
            return result;
        }
    }

    /* Not found. */
    MVM_gc_root_temp_pop_n(tc, 3);
    MVM_spesh_frame_walker_cleanup(tc, fw);
    if (dlog) {
        fprintf(dlog, "N %s %d %d %d %d %"PRIu64" %"PRIu64" %"PRIu64"\n", c_name, fcost, icost, ecost, xcost, last_time, start_time, uv_hrtime());
        fflush(dlog);
        MVM_free(c_name);
        tc->instance->dynvar_log_lasttime = uv_hrtime();
    }
    *found_frame = NULL;
    return NULL;
}
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name,
        MVMuint16 *type, MVMFrame *initial_frame, MVMint32 vivify, MVMFrame **found_frame) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init(tc, &fw, initial_frame, 0);
    return MVM_frame_find_dynamic_using_frame_walker(tc, &fw, name, type, initial_frame,
            vivify, found_frame);
}

void MVM_frame_getdynlex_with_frame_walker(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
                                                  MVMString *name, MVMRegister *r) {
    MVMuint16 type;
    MVMFrame *found_frame;
    MVMRegister *lex_reg = MVM_frame_find_dynamic_using_frame_walker(tc, fw, name, &type,
            MVM_spesh_frame_walker_current_frame(tc, fw), 1, &found_frame);
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
            case MVM_reg_uint64:
                result_type = (*tc->interp_cu)->body.hll_config->int_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing int box type (for a uint)");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_uint(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->u64);
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
    if (result) {
        r->o = result;
    }
    else {
        MVMCode *resolver = tc->cur_frame->static_info->body.cu->body.dynamic_resolver;
        if (resolver) {
            MVMCallStackArgsFromC *args_record = MVM_callstack_allocate_args_from_c(tc,
                    MVM_callsite_get_common(tc, MVM_CALLSITE_ID_STR));
            args_record->args.source[0].s = name;
            MVM_frame_dispatch_from_c(tc, resolver, args_record, r, MVM_RETURN_OBJ);
        }
        else {
            r->o = tc->instance->VMNull;
        }
    }
}
void MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame, MVMRegister *r) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init(tc, &fw, cur_frame, 0);
    MVM_frame_getdynlex_with_frame_walker(tc, &fw, name, r);
}

void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMFrame *found_frame;
    MVMRegister *lex_reg;
    MVMROOT2(tc, name, value, {
        lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame, 0, &found_frame);
    });
    if (!lex_reg) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Dynamic variable '%s' not found",
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
    if (MVM_LIKELY(f->static_info->body.num_lexicals != 0)) {
        MVMuint32 idx = MVM_get_lexical_by_name(tc, f->static_info, name);
        if (idx != MVM_INDEX_HASH_NOT_FOUND)
            return &f->env[idx];
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
    if (f->static_info->body.num_lexicals) {
        MVMuint32 idx = MVM_get_lexical_by_name(tc, f->static_info, name);
        if (idx != MVM_INDEX_HASH_NOT_FOUND && f->static_info->body.lexical_types[idx] == type) {
            MVMRegister *result = &f->env[idx];
            if (type == MVM_reg_obj && !result->o)
                MVM_frame_vivify_lexical(tc, f, idx);
            return result;
        }
    }
    return NULL;
}

/* Translates a register kind into a primitive storage spec constant. */
MVMuint16 MVM_frame_translate_to_primspec(MVMThreadContext *tc, MVMuint16 kind) {
    switch (MVM_EXPECT(kind, MVM_reg_obj)) {
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
            MVM_exception_throw_adhoc(tc,
                "Unhandled lexical type '%s' in lexprimspec",
                MVM_reg_get_debug_name(tc, kind));
    }
}

/* Returns the primitive type specification for a lexical. */
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    if (f->static_info->body.num_lexicals) {
        MVMuint32 idx = MVM_get_lexical_by_name(tc, f->static_info, name);
        if (idx != MVM_INDEX_HASH_NOT_FOUND)
            return MVM_frame_translate_to_primspec(tc,
                    f->static_info->body.lexical_types[idx]);
    }
    {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Frame has no lexical with name '%s'",
            c_name);
    }
}

/* Gets, allocating if needed, the frame extra data structure for the given
 * frame. This is used to hold data that only a handful of frames need. */
MVMFrameExtra * MVM_frame_extra(MVMThreadContext *tc, MVMFrame *f) {
    if (!f->extra)
        f->extra = MVM_calloc(1, sizeof(MVMFrameExtra));
    return f->extra;
}

/* Gets the code object of the caller, provided there is one. Works even in
 * the face that the caller was an inline (however, the current frame that is
 * using the op must not be itself inlined). */
MVMObject * MVM_frame_caller_code(MVMThreadContext *tc) {
    MVMObject *result;
    MVMFrame *f = tc->cur_frame;
    if (f->caller) {
        MVMSpeshFrameWalker fw;
        MVM_spesh_frame_walker_init(tc, &fw, f, 0);
        MVM_spesh_frame_walker_move_caller(tc, &fw);
        result = MVM_spesh_frame_walker_get_code(tc, &fw);
        MVM_spesh_frame_walker_cleanup(tc, &fw);
    }
    else {
        result = tc->instance->VMNull;
    }
    return result;
}
