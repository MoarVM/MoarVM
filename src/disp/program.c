#include "moar.h"

/* Run a dispatch callback, which will record a dispatch program. */
static void run_dispatch(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMDispDefinition *disp, MVMObject *capture, MVMuint32 *thunked) {
    MVMCallsite *disp_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    record->current_capture.o = capture;
    MVMArgs dispatch_args = {
        .callsite = disp_callsite,
        .source = &(record->current_capture),
        .map = MVM_args_identity_map(tc, disp_callsite)
    };
    MVMObject *dispatch = disp->dispatch;
    if (REPR(dispatch)->ID == MVM_REPR_ID_MVMCFunction) {
        record->outcome.kind = MVM_DISP_OUTCOME_FAILED;
        ((MVMCFunction *)dispatch)->body.func(tc, dispatch_args);
        MVM_callstack_unwind_dispatcher(tc, thunked);
    }
    else if (REPR(dispatch)->ID == MVM_REPR_ID_MVMCode) {
        record->outcome.kind = MVM_DISP_OUTCOME_EXPECT_DELEGATE;
        record->outcome.delegate_disp = NULL;
        record->outcome.delegate_capture = NULL;
        tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
        MVM_frame_dispatch(tc, (MVMCode *)dispatch, dispatch_args, -1);
        if (thunked)
            *thunked = 1;
    }
    else {
        MVM_panic(1, "dispatch callback only supported as a MVMCFunction or MVMCode");
    }
}
void MVM_disp_program_run_dispatch(MVMThreadContext *tc, MVMDispDefinition *disp, MVMObject *capture) {
    /* Push a dispatch recording frame onto the callstack; this is how we'll
     * keep track of the current recording state. */
    MVMCallStackDispatchRecord *record = MVM_callstack_allocate_dispatch_record(tc);
    record->initial_capture = capture;
    record->derived_captures = NULL;
    run_dispatch(tc, record, disp, capture, NULL);
}

/* Takes a capture and makes sure it's one we're aware of in the currently
 * being recorded dispatch program. */
static void ensure_known_capture(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMObject *capture) {
    if (capture != record->initial_capture) {
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

/* Start tracking an argument from the specified capture. This allows us to
 * apply guards against it. */
MVMObject * MVM_disp_program_record_track_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index) {
    /* Ensure the incoming capture is known. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);

    // XXX TODO record info about this

    /* Obtain the argument and wrap it in a tracked object. */
    MVMRegister value;
    MVMCallsiteFlags kind;
    MVM_capture_arg_pos(tc, capture, index, &value, &kind);
    return MVM_tracked_create(tc, value, kind);
}

/* Record a guard of the current type of the specified tracked value. */
void MVM_disp_program_record_guard_type(MVMThreadContext *tc, MVMObject *tracked) {
    // TODO
}

/* Record a guard of the current concreteness of the specified tracked value. */
void MVM_disp_program_record_guard_concreteness(MVMThreadContext *tc, MVMObject *tracked) {
    // TODO
}

/* Record a guard of the current value of the specified tracked value. */
void MVM_disp_program_record_guard_literal(MVMThreadContext *tc, MVMObject *tracked) {
    // TODO
}

/* Record a guard that the specified tracked value must not be a certain object
 * literal. */
void MVM_disp_program_record_guard_not_literal_obj(MVMThreadContext *tc,
       MVMObject *tracked, MVMObject *object) {
    // TODO
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

/* Record that we insert a tracked value into a capture. Also perform the insert
 * on the value that was read. */
MVMObject * MVM_disp_program_record_capture_insert_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 index, MVMObject *tracked) {
    /* Ensure the incoming capture and tracked values are known. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);
    // XXX TODO tracked value

    /* Calculate the new capture and add it to the derived capture set to keep
     * it alive. */
    MVMObject *new_capture = MVM_capture_insert_arg(tc, capture, index,
            ((MVMTracked *)tracked)->body.kind, ((MVMTracked *)tracked)->body.value);
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

/* Record that we insert a new constant argument from a capture. Also perform the
 * insert, resulting in a new capture without a new argument inserted at the
 * given index. */
MVMObject * MVM_disp_program_record_capture_insert_constant_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 index, MVMCallsiteFlags kind, MVMRegister value) {
    /* Ensure the incoming capture is known. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);

    /* Calculate the new capture and add it to the derived capture set to keep
     * it alive. */
    MVMObject *new_capture = MVM_capture_insert_arg(tc, capture, index, kind, value);
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

/* Record a delegation from one dispatcher to another. */
void MVM_disp_program_record_delegate(MVMThreadContext *tc, MVMString *dispatcher_id,
        MVMObject *capture) {
    /* We can only do a single dispatcher delegation. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->outcome.delegate_disp != NULL)
        MVM_exception_throw_adhoc(tc,
                "Can only call dispatcher-delegate once in a dispatch callback");

    /* Resolve the dispatcher we wish to delegate to, and ensure that this
     * capture is one we know about. Then stash the info. */
    MVMDispDefinition *disp = MVM_disp_registry_find(tc, dispatcher_id);
    ensure_known_capture(tc, record, capture);
    record->outcome.delegate_disp = disp;
    record->outcome.delegate_capture = capture;
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

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_BYTECODE;
    record->outcome.code = result;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Record a program terminator that invokes an MVMCFunction object, which is to be
 * considered a constant. */
void MVM_disp_program_record_c_code_constant(MVMThreadContext *tc, MVMCFunction *result,
        MVMObject *capture) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);
    // XXX TODO

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_CFUNCTION;
    record->outcome.c_func = result->body.func;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Called when we have finished recording a dispatch program. */
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record,
        MVMuint32 *thunked) {
    // TODO compile program, update inline cache

    /* Set the result in place. */
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_FAILED:
            return 1;
        case MVM_DISP_OUTCOME_EXPECT_DELEGATE:
            if (record->outcome.delegate_disp)
                run_dispatch(tc, record, record->outcome.delegate_disp,
                        record->outcome.delegate_capture, thunked);
            else
                MVM_exception_throw_adhoc(tc, "Dispatch callback failed to delegate to a dispatcher");
            return 0;
        case MVM_DISP_OUTCOME_VALUE: {
            MVMFrame *caller = MVM_callstack_record_to_frame(record->common.prev);
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
        }
        case MVM_DISP_OUTCOME_BYTECODE:
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
            MVM_frame_dispatch(tc, record->outcome.code, record->outcome.args, -1);
            if (thunked)
                *thunked = 1;
            return 0;
        case MVM_DISP_OUTCOME_CFUNCTION:
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
            record->outcome.c_func(tc, record->outcome.args);
            return 1;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}
