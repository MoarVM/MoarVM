#include "moar.h"

#define DUMP_RESUMPTIONS 0

/* Set up resumption data for the situation where the dispatch that is to be
 * resumed is either in unspecialized code or an untranslated dispatch program
 * in specialized code (that is, left as sp_dispatch). In this case, there
 * is a dispatch recorded or dispatch run record on the callstack, and we'll
 * use that for storage of resumption state, to find the original dispatch
 * args and temporaries, etc. */
static void finish_resumption_data(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMDispResumptionState *state, MVMuint32 offset) {
    data->resumption = &(data->dp->resumptions[offset]);
#if DUMP_RESUMPTIONS
    fprintf(stderr, "    choosing %s\n",
            MVM_string_utf8_encode_C_string(tc, data->resumption->disp->id));
#endif
    for (MVMuint32 i = 0; i < offset; i++)
        state = state->next;
    data->state_ptr = &(state->state);
}
static MVMuint32 setup_resumption(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMDispProgram *dp, MVMArgs *arg_info, MVMDispResumptionState *state,
        MVMRegister *temps, MVMuint32 *exhausted) {
    /* Did the dispatch program set up any static resumptions, and are there at
     * least as many as we've already passed? */
    if (dp->num_resumptions > *exhausted) {
        /* Yes; do we have dispatch state for them already? */
        if (!state->disp) {
            /* No state; set it up. */
            MVMDispResumptionState *prev = NULL;
            for (MVMuint32 i = 0; i < dp->num_resumptions; i++) {
                /* For the innermost (or only) one, we write into the record.
                 * For more, we need to allocate. */
                MVMDispResumptionState *target = prev
                    ? MVM_malloc(sizeof(MVMDispResumptionState))
                    : state;
                target->disp = dp->resumptions[i].disp;
                target->state = tc->instance->VMNull;
                target->next = NULL;
                if (prev)
                    prev->next = target;
                prev = target;
            }

            /* Set up the resumption data for the requested dispatcher. */
            data->dp = dp;
            data->arg_source = MVMDispResumptionArgUntranslated;
            data->untran.initial_arg_info = arg_info;
            data->untran.temps = temps;
            finish_resumption_data(tc, data, state, *exhausted);
            return 1;
        }
        else {
            /* Already have state record set up. */
            data->dp = dp;
            data->arg_source = MVMDispResumptionArgUntranslated;
            data->untran.initial_arg_info = arg_info;
            data->untran.temps = temps;
            finish_resumption_data(tc, data, state, *exhausted);
            return 1;
        }
    }
    *exhausted -= dp->num_resumptions;
    return 0;
}

/* Set up resumption data in the situation that the dispatch we'll resume was
 * translated into ops. In this case, there is a register for storing any
 * dispatch state object, and we also have registers holding all of the args
 * and temps that we might need. */
static void setup_translated_resumption(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMSpeshResumeInit *ri, MVMFrame *frame) {
    data->dp = ri->dp;
    data->resumption = &(ri->dp->resumptions[ri->res_idx]);
    data->state_ptr = &(frame->work[ri->state_register].o);
    data->arg_source = MVMDispResumptionArgTranslated;
    data->tran.work = frame->work;
    data->tran.map = ri->init_registers;
}

/* Set up resumption data in the sitaution that the dispatch we'll resume was
 * translated, but then we deoptimized and uninlined it. */
static void setup_deopted_resumption(MVMThreadContext *tc,  MVMDispResumptionData *data,
        MVMCallStackDeoptedResumeInit *dri) {
    data->dp = dri->dp;
    data->resumption = dri->dpr;
    data->state_ptr = &(dri->state);
    data->arg_source = MVMDispResumptionArgTranslated;
    data->tran.work = dri->args;
    data->tran.map = MVM_args_identity_map(tc, dri->dpr->init_callsite);
}

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. If found, populates the struct pointed to by
 * the data parameter and returns a non-zero value. */
static MVMint32 should_keep_searching(MVMThreadContext *tc, MVMDispProgram *dp) {
    return !dp || dp->num_resumptions == 0 || dp->ops[0].code == MVMDispOpcodeStartResumption;
}
static MVMuint32 find_internal(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 exhausted, MVMint32 caller) {
    /* Create iterator, which is over both dispatch records and frames. */
#if DUMP_RESUMPTIONS
    fprintf(stderr, "looking for a resumption; exhausted = %d, caller = %d\n",
            exhausted, caller);
#endif
    MVMCallStackIterator iter;
    MVM_callstack_iter_resumeable_init(tc, &iter, tc->stack_top);

    /* How many frames (including inlined ones) do we need to see before we
     * start looking for something to resume? We never want to look at the
     * current frame (or inline), and we need to go one more out if we're in
     * caller mode. */
    MVMint32 frames_to_skip = caller ? 2 : 1;
    MVMint32 seen_frame = 0;
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_BIND_CONTROL:
                /* If this is on the stack top, then we just exited a frame
                 * due to a bind failure and are doing a resume as a result.
                 * The frame we just left counts as 1. */
                if (!seen_frame && frames_to_skip)
                    frames_to_skip--;
                break;
            case MVM_CALLSTACK_RECORD_FRAME:
            case MVM_CALLSTACK_RECORD_HEAP_FRAME:
            case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
            case MVM_CALLSTACK_RECORD_DEOPT_FRAME: {
                seen_frame = 1;
                MVMFrame *frame = MVM_callstack_record_to_frame(cur);
                MVMSpeshCandidate *cand = frame->spesh_cand;
                if (cand) {
                    /* Specialized frame. Are there resume inits? */
                    if (cand->body.num_resume_inits) {
                        /* Yes, get the deopt idx and look for if we have any
                         * such entries at the current call. There are two cases:
                         * 1. We're the top frame. In that case, we're sat on a
                         *    sp_dispatch_*, since at least for now any resuming
                         *    (not resumable) dispatch programs are not translated,
                         *    thus we're effectively inactive and can find the
                         *    deopt index this way.
                         * 2. Failing that, we're simply an inactive frame.
                         * We always look first for the dispatch that we're "on";
                         * it is the one in the most nested inline, if there are
                         * any inlines. */
                        MVMint32 deopt_idx = MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc,
                                frame, cand);
                        if (deopt_idx == -1)
                            MVM_oops(tc, "Failed to find deopt index when processing resume");
                        MVMuint32 i;
                        MVMint32 saw_dispatch_with_resumptions = 0;
                        for (i = 0; i < cand->body.num_resume_inits; i++) {
                            if (cand->body.resume_inits[i].deopt_idx == deopt_idx) {
                                if (exhausted == 0) {
                                    MVMSpeshResumeInit *ri = &(cand->body.resume_inits[i]);
                                    setup_translated_resumption(tc, data, ri, frame);
                                    return 1;
                                }
                                else {
                                    exhausted--;
                                    saw_dispatch_with_resumptions = 1;
                                }
                            }
                        }
                        if (saw_dispatch_with_resumptions)
                            return 0;

                        /* Now walk any inlines to see if we're in any of those. */
                        if (cand->body.num_inlines) {
                            MVMuint32 deopt_offset = MVM_spesh_deopt_bytecode_pos(
                                    cand->body.deopts[deopt_idx * 2 + 1]);
                            MVMuint32 i;
                            for (i = 0; i < cand->body.num_inlines; i++) {
                                /* Check if we're in this inline. */
                                MVMuint32 start = cand->body.inlines[i].start;
                                MVMuint32 end = cand->body.inlines[i].end;
                                if (!(deopt_offset > start && deopt_offset <= end))
                                    continue;

                                /* Does it have any resume inits? If so, are
                                 * any applicable? */
                                if (cand->body.inlines[i].first_spesh_resume_init != -1) {
                                    MVMint16 j = cand->body.inlines[i].first_spesh_resume_init;
                                    while (j <= cand->body.inlines[i].last_spesh_resume_init) {
                                        if (exhausted == 0) {
                                            MVMSpeshResumeInit *ri = &(cand->body.resume_inits[j]);
                                            setup_translated_resumption(tc, data, ri, frame);
                                            return 1;
                                        }
                                        else {
                                            exhausted--;
                                        }
                                        j++;
                                    }
                                    /* There were resumptions, but none applied; we should
                                     * not search beyond the innermost dispatch for them. */
                                    return 0;
                                }

                                /* Count this inline as a frame. */
                                if (frames_to_skip)
                                    frames_to_skip--;
                            }
                        }

                        /* No matching resume init found; account for the frame. */
                        if (frames_to_skip)
                            frames_to_skip--;
                    }
                    else {
                        /* No resume inits. TODO But maybe inlines, factor those
                         * in to the skipping logic here. */
                        if (frames_to_skip)
                            frames_to_skip--;
                    }
                }
                else {
                    /* Non-specialized, so there'll be dispatch records if
                     * there is anything to resume. It counts against the
                     * frames we wish to skip, however. */
                    if (frames_to_skip)
                        frames_to_skip--;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                if (!frames_to_skip) {
                    MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
#if DUMP_RESUMPTIONS
                    fprintf(stderr, "  Visiting a recorded dispatch record\n");
#endif
                    if (dr->produced_dp && setup_resumption(tc, data, dr->produced_dp,
                                &(dr->arg_info), &(dr->resumption_state), dr->temps, &exhausted))
                        return 1;
                    if (!should_keep_searching(tc, dr->produced_dp))
                        return 0;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                if (!frames_to_skip) {
                    MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
#if DUMP_RESUMPTIONS
                    fprintf(stderr, "  Visiting a run dispatch record\n");
#endif
                    if (dr->chosen_dp && setup_resumption(tc, data, dr->chosen_dp,
                                &(dr->arg_info), &(dr->resumption_state), dr->temps, &exhausted))
                        return 1;
                    if (!should_keep_searching(tc, dr->chosen_dp))
                        return 0;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DEOPTED_RESUME_INIT:
                if (!frames_to_skip) {
                    /* Deoptimized resume init args. These look relatively
                     * like the translated case. */
#if DUMP_RESUMPTIONS
                    fprintf(stderr, "  Visiting a deoptimized resume init record\n");
#endif
                    if (exhausted == 0) {
                        setup_deopted_resumption(tc, data,
                            (MVMCallStackDeoptedResumeInit *)cur);
                        return 1;
                    }
                    else {
                        exhausted--;
                    }
                }
                break;
            case MVM_CALLSTACK_RECORD_CONTINUATION_TAG:
                /* We should never find resumptions the other side of a
                 * continuation tag. The reason is that we'll retain
                 * pointers into the lower parts of the call stack, at
                 * the point where the dispatch resumed. This is safe as
                 * we know that a callee will always complete before its
                 * caller, but if there's a continuation tag between the
                 * two, it's possible that the caller would exit and leave
                 * dangling pointers in the callee at the time that it is
                 * spliced back onto the stack and run. */
                return 0;
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data,
                                       MVMuint32 exhausted) {
    return find_internal(tc, data, exhausted, 0);
}

/* Skip to our caller, and then find the current dispatch. */
MVMuint32 MVM_disp_resume_find_caller(MVMThreadContext *tc, MVMDispResumptionData *data,
                                      MVMuint32 exhausted) {
    return find_internal(tc, data, exhausted, 1);
}

/* Get the resume initialization state argument at the specified index. */
MVMRegister MVM_disp_resume_get_init_arg(MVMThreadContext *tc, MVMDispResumptionData *data,
                                         MVMuint32 arg_idx) {
    MVMDispProgramResumption *resumption = data->resumption;
    if (resumption->init_values) {
        MVMDispProgramResumptionInitValue *value = &(resumption->init_values[arg_idx]);
        MVMRegister result;
        switch (value->source) {
            case MVM_DISP_RESUME_INIT_ARG: {
                if (data->arg_source == MVMDispResumptionArgTranslated) {
                    result = data->tran.work[data->tran.map[arg_idx]];
                }
                else {
                    MVMArgs *args = data->untran.initial_arg_info;
                    result = args->source[args->map[value->index]];
                }
                break;
            }
            case MVM_DISP_RESUME_INIT_CONSTANT_OBJ:
                result.o = (MVMObject *)data->dp->gc_constants[value->index];
                break;
            case MVM_DISP_RESUME_INIT_CONSTANT_INT:
                result.i64 = data->dp->constants[value->index].i64;
                break;
            case MVM_DISP_RESUME_INIT_CONSTANT_NUM:
                result.n64 = data->dp->constants[value->index].n64;
                break;
            case MVM_DISP_RESUME_INIT_TEMP:
                if (data->arg_source == MVMDispResumptionArgTranslated)
                    result = data->tran.work[data->tran.map[arg_idx]];
                else
                    result = data->untran.temps[value->index];
                break;
            default:
                MVM_oops(tc, "unknown resume init arg source");
        }
        return result;
    }
    else {
        /* Simple case where they are the initial arguments to the dispatch. */
        if (data->arg_source == MVMDispResumptionArgTranslated) {
            return data->tran.work[data->tran.map[arg_idx]];
        }
        else {
            MVMArgs *args = data->untran.initial_arg_info;
            return args->source[args->map[arg_idx]];
        }
    }
}

/* Mark the resumption state. */
void MVM_disp_resume_mark_resumption_state(MVMThreadContext *tc, MVMDispResumptionState *res_state,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    /* Ensure it's valid (if top level is, stack will be). */
    if (!res_state->disp)
        return;

    /* Mark the state along the linked list. */
    MVMDispResumptionState *current = res_state;
    while (current) {
        if (worklist)
            MVM_gc_worklist_add(tc, worklist, &(current->state));
        else
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot,
                (MVMCollectable *)current->state, "Dispatch resumption state");
        current = current->next;
    }
}

/* Free any memory associated with a linked list of resumption states. */
void MVM_disp_resume_destroy_resumption_state(MVMThreadContext *tc,
        MVMDispResumptionState *res_state) {
    /* First entry lives on the stack, so don't consider it. */
    MVMDispResumptionState *current = res_state->next;
    while (current) {
        MVMDispResumptionState *next = current->next;
        MVM_free(current);
        current = next;
    }
}
