#include "moar.h"

/* Looks down the callstack to find the dispatch that we are resuming, starting
 * from the indicated start point. */
MVMuint32 find_internal(MVMThreadContext *tc, MVMCallStackRecord *start,
        MVMDispProgram **found_disp_program, MVMCallStackRecord **found_record) {
    MVMCallStackIterator iter;
    MVM_callstack_iter_dispatch_init(tc, &iter, start);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *cur = MVM_callstack_iter_current(tc, &iter);
        switch (cur->kind) {
            case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
                MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)cur;
                if (dr->produced_dp && dr->produced_dp->num_resumptions) {
                    *found_disp_program = dr->produced_dp;
                    *found_record = cur;
                    return 1;
                }
                break;
            }
            case MVM_CALLSTACK_RECORD_DISPATCH_RUN: {
                MVMCallStackDispatchRun *dr = (MVMCallStackDispatchRun *)cur;
                if (dr->chosen_dp && dr->chosen_dp->num_resumptions) {
                    *found_disp_program = dr->chosen_dp;
                    *found_record = cur;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Looks down the callstack to find the dispatch that we are resuming. */
MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispProgram **found_disp_program,
        MVMCallStackRecord **found_record) {
    return find_internal(tc, tc->stack_top, found_disp_program, found_record);
}

/* Given a callstack record, obtain a Capture object that has the resume init
 * arguments. */
MVMObject * MVM_disp_resume_init_capture(MVMThreadContext *tc, MVMCallStackRecord *record,
                                         MVMuint32 resumption_idx) {
    switch (record->kind) {
        case MVM_CALLSTACK_RECORD_DISPATCH_RECORDED: {
            MVMCallStackDispatchRecord *dr = (MVMCallStackDispatchRecord *)record;
            MVMDispProgram *dp = dr->produced_dp;
            MVMDispProgramResumption *res = &(dp->resumptions[resumption_idx]);
            if (res->init_values) {
                MVM_oops(tc, "complex case of resume init state NYI");
            }
            else {
                return dr->rec.initial_capture.capture;
            }
        }
        default:
            MVM_oops(tc, "resume init capture run case NYI");
    }
}
