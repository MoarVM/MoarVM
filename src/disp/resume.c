#include "moar.h"

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. If found, populates the struct pointed to by
 * the data parameter and returns a non-zero value. */
MVMuint32 find_internal(MVMThreadContext *tc, MVMCallStackRecord *start,
        MVMDispResumptionData *data) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_dispatch_init(tc, &iter, start);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        // TODO this for now assumes there's only a single resumable dispatch;
        // that shall need to change
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
                if (dr->produced_dp && dr->produced_dp->num_resumptions) {
                    data->dp = dr->produced_dp;
                    data->initial_arg_info = &(dr->arg_info);
                    data->resumption = &(data->dp->resumptions[0]);
                    return 1;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
                if (dr->chosen_dp && dr->chosen_dp->num_resumptions) {
                    data->dp = dr->chosen_dp;
                    data->initial_arg_info = &(dr->arg_info);
                    data->resumption = &(data->dp->resumptions[0]);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data) {
    return find_internal(tc, tc->stack_top, data);
}
