#include "moar.h"

static MVMuint32 setup_resumption(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMDispProgram *dp, MVMArgs *arg_info, MVMDispResumptionState *state) {
    /* Did the dispatch program set up any static resumptions? */
    if (dp->num_resumptions > 0) {
        /* Yes; do we have dispatch state for them already? */
        if (!state->disp) {
            /* No state; set it up. */
            MVMint32 i;
            MVMDispResumptionState *prev = NULL;
            for (i = dp->num_resumptions - 1; i >= 0; i--) {
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

            /* We take the innermost dispatcher. */
            data->dp = dp;
            data->initial_arg_info = arg_info;
            data->resumption = &(data->dp->resumptions[dp->num_resumptions - 1]);
            data->state_ptr = &(state->state);
            return 1;
        }
        else {
            /* Already have state record set up. */
            // TODO Stacked resumable dispatchers need handling here
            data->dp = dp;
            data->initial_arg_info = arg_info;
            data->resumption = &(data->dp->resumptions[dp->num_resumptions - 1]);
            data->state_ptr = &(state->state);
            return 1;
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. If found, populates the struct pointed to by
 * the data parameter and returns a non-zero value. */
static MVMuint32 find_internal(MVMThreadContext *tc, MVMCallStackRecord *start,
        MVMDispResumptionData *data) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_dispatch_init(tc, &iter, start);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
                if (dr->produced_dp && setup_resumption(tc, data, dr->produced_dp,
                            &(dr->arg_info), &(dr->resumption_state)))
                    return 1;
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
                if (dr->chosen_dp && setup_resumption(tc, data, dr->chosen_dp,
                            &(dr->arg_info), &(dr->resumption_state)))
                    return 1;
                break;
            }
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data) {
    return find_internal(tc, tc->stack_top, data);
}

/* Get the resume initialization state argument at the specified index. */
MVMRegister MVM_disp_resume_get_init_arg(MVMThreadContext *tc, MVMDispResumptionData *data,
                                         MVMuint32 arg_idx) {
    MVMDispProgramResumption *resumption = data->resumption;
    if (resumption->init_values) {
        MVM_oops(tc, "complex case of getting resume init args NYI");
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
