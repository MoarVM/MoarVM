#include "moar.h"

/* Takes a capture and makes sure it's one we're aware of in the currently
 * being recorded dispatch program. */
static void ensure_known_capture(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMObject *capture) {
    if (capture != record->initial_capture.o) {
        int found = 0;
        if (record->derived_captures) {
            MVMint64 elems = MVM_repr_elems(tc, record->derived_captures);
            MVMint64 i;
            for (i = 0; i < elems; i++) {
                if (MVM_repr_at_pos_o(tc, record->derived_captures, i) == capture) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found)
            MVM_exception_throw_adhoc(tc, "Can only drop from a capture known in this dispatch");
    }
}

/* Record that we drop an argument from a capture. Also perform the drop,
 * resulting in a new capture without that argument. */
MVMObject * MVM_disp_program_record_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 idx) {
    /* Ensure the incoming capture is known. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);

    /* Calculate the new capture and add it to the derived capture set to keep
     * it alive. */
    MVMObject *new_capture = MVM_capture_drop_arg(tc, capture, idx);
    if (!record->derived_captures) {
        MVMROOT(tc, new_capture, {
            record->derived_captures = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        });
    }
    MVM_repr_push_o(tc, record->derived_captures, new_capture);

    // XXX TODO record info about this

    /* Evaluate to the new capture, for the running dispatch function. */
    return new_capture;
}

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

/* Record a program terminator that invokes an MVMCode object, which is to be
 * considered a constant (e.g. so long as the guards that come before this
 * point match, the thing to invoke is always this code object). */
void MVM_disp_program_record_code_constant(MVMThreadContext *tc, MVMCode *result, MVMObject *capture) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);
    // XXX TODO

    /* Put the return value in place. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_BYTECODE;
    record->outcome.code = result;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
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
        case MVM_DISP_OUTCOME_BYTECODE:
            tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
            MVM_frame_dispatch(tc, record->outcome.code, record->outcome.args, -1);
            return 0;
            break;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}
