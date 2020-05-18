#include "moar.h"

/* Record a program terminator that is a constant boject value. */
void MVM_disp_program_record_result_constant(MVMThreadContext *tc, MVMObject *result) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    // XXX TODO

    /* Put the return value in place. */
    record->outcome.kind = MVM_DISP_OUTCOME_VALUE;
    record->outcome.result_value.o = result;
    record->outcome.result_kind = MVM_reg_obj;
}

/* Record a program terminator that reads the value from an argument capture. */
void MVM_disp_program_record_result_capture_value(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMRegister value;
    MVMCallsiteFlags value_type;
    MVMuint8 reg_type;
    MVM_capture_arg_pos(tc, capture, index, &value, &value_type);
    switch (value_type) {
        case MVM_CALLSITE_ARG_OBJ: reg_type = MVM_reg_obj; break;
        case MVM_CALLSITE_ARG_INT: reg_type = MVM_reg_int64; break;
        case MVM_CALLSITE_ARG_NUM: reg_type = MVM_reg_num64; break;
        case MVM_CALLSITE_ARG_STR: reg_type = MVM_reg_str; break;
        default: MVM_oops(tc, "Unknown capture value type in boot-value dispatch");
    }
    // XXX TODO

    /* Put the return value in place. */
    record->outcome.kind = MVM_DISP_OUTCOME_VALUE;
    record->outcome.result_value = value;
    record->outcome.result_kind = reg_type;
}

/* Called when we have finished recording a dispatch program. */
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record) {
    // TODO compile program, update inline cache

    /* Set the result in place. */
    MVMFrame *caller = MVM_callstack_record_to_frame(record->common.prev);
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_VALUE:
            switch (record->outcome.result_kind) {
                case MVM_reg_obj:
                    MVM_args_set_dispatch_result_obj(tc, caller, record->outcome.result_value.o);
                    break;
                case MVM_reg_int64:
                    MVM_args_set_dispatch_result_int(tc, caller, record->outcome.result_value.i64);
                    break;
                case MVM_reg_num64:
                    MVM_args_set_dispatch_result_num(tc, caller, record->outcome.result_value.n64);
                    break;
                case MVM_reg_str:
                    MVM_args_set_dispatch_result_str(tc, caller, record->outcome.result_value.s);
                    break;
                default:
                    MVM_oops(tc, "Unknown result kind in dispatch value outcome");
            }
            return 1;
            break;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}
