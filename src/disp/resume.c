#include "moar.h"

static void finish_resumption_data(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMDispResumptionState *state, MVMuint32 offset) {
    data->resumption = &(data->dp->resumptions[offset]);
    for (MVMuint32 i = 0; i < offset; i++)
        state = state->next;
    data->state_ptr = &(state->state);
}
static MVMuint32 setup_resumption(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMDispProgram *dp, MVMArgs *arg_info, MVMDispResumptionState *state,
        MVMuint32 exhausted) {
    /* Did the dispatch program set up any static resumptions, and are there at
     * least as many as we've already passed? */
    if (dp->num_resumptions > exhausted) {
        /* Yes; do we have dispatch state for them already? */
        if (!state->disp) {
            /* No state; set it up. */
            MVMDispResumptionState *prev = NULL;
            for (MVMuint32 i = 0; i < dp->num_resumptions; i++) {
                /* For the innermost (or only) one, we write into the record.
                 * For more, we need to allocate. */
                MVMDispResumptionState *target = prev
                    ? MVM_fixed_size_alloc(tc, tc->instance->fsa, sizeof(MVMDispResumptionState))
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
            data->initial_arg_info = arg_info;
            finish_resumption_data(tc, data, state, exhausted);
            return 1;
        }
        else {
            /* Already have state record set up. */
            data->dp = dp;
            data->initial_arg_info = arg_info;
            finish_resumption_data(tc, data, state, exhausted);
            return 1;
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. If found, populates the struct pointed to by
 * the data parameter and returns a non-zero value. */
static MVMuint32 find_internal(MVMThreadContext *tc, MVMCallStackRecord *start,
        MVMDispResumptionData *data, MVMuint32 exhausted) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_dispatch_init(tc, &iter, start);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
                if (dr->produced_dp && setup_resumption(tc, data, dr->produced_dp,
                            &(dr->arg_info), &(dr->resumption_state), exhausted))
                    return 1;
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
                if (dr->chosen_dp && setup_resumption(tc, data, dr->chosen_dp,
                            &(dr->arg_info), &(dr->resumption_state), exhausted))
                    return 1;
                break;
            }
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data,
                                       MVMuint32 exhausted) {
    return find_internal(tc, tc->stack_top, data, exhausted);
}

/* Skip to our caller, and then find the current dispatch. */
MVMuint32 MVM_disp_resume_find_caller(MVMThreadContext *tc, MVMDispResumptionData *data,
                                      MVMuint32 exhausted) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_init(tc, &iter, tc->stack_top);
    if (!MVM_callstack_iter_move_next(tc, &iter)) // Current frame
        return 0;
    if (!MVM_callstack_iter_move_next(tc, &iter)) // Caller frame
        return 0;
    return find_internal(tc, MVM_callstack_iter_current(tc, &iter), data, exhausted);
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
                MVMArgs *args = data->initial_arg_info;
                result = args->source[args->map[value->index]];
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
            default:
                MVM_oops(tc, "unknown resume init arg source");
        }
        return result;
    }
    else {
        /* Simple case where they are the initial arguments to the dispatch. */
        MVMArgs *args = data->initial_arg_info;
        return args->source[args->map[arg_idx]];
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
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMDispResumptionState), current);
        current = next;
    }
}
