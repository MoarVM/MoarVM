#include "moar.h"

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. */
MVMuint32 find_internal(MVMThreadContext *tc, MVMCallStackRecord *start,
        MVMDispProgram **found_disp_program) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_dispatch_init(tc, &iter, start);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
                if (dr->produced_dp && dr->produced_dp->num_resumptions) {
                    *found_disp_program = dr->produced_dp;
                    return 1;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
                if (dr->chosen_dp && dr->chosen_dp->num_resumptions) {
                    *found_disp_program = dr->chosen_dp;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispProgram **found_disp_program) {
    return find_internal(tc, tc->stack_top, found_disp_program);
}
