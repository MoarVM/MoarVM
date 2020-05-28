#include "moar.h"

/* Debug dumping, to figure out what we're recording and what programs we are
 * inferring from those recordings. */
#define DUMP_RECORDINGS     0
#define DUMP_PROGRAMS       0

#if DUMP_RECORDINGS
static void dump_recording_capture(MVMThreadContext *tc,
        MVMDispProgramRecordingCapture *capture, MVMuint32 indent) {
    char *indent_str = alloca(indent + 1);
    memset(indent_str, ' ', indent);
    indent_str[indent] = '\0';
    switch (capture->transformation) {
        case MVMDispProgramRecordingInitial:
            fprintf(stderr, "%sInitial\n", indent_str);
            break;
        case MVMDispProgramRecordingDrop:
            fprintf(stderr, "%sDrop argument %d\n", indent_str, capture->index);
            break;
        case MVMDispProgramRecordingInsert:
            fprintf(stderr, "%sInsert value %d at %d\n", indent_str, capture->value_index,
                    capture->index);
            break;
        default:
            fprintf(stderr, "%sUnknown transforamtion\n", indent_str);
            break;
    }
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(capture->captures); i++)
        dump_recording_capture(tc, &(capture->captures[i]), indent + 2);
}
static void dump_recording_values(MVMThreadContext *tc, MVMDispProgramRecording *rec) {
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        switch (v->source) {
            case MVMDispProgramRecordingCaptureValue:
                fprintf(stderr, "    %d Initial argument %d\n", i, v->capture.index);
                break;
            case MVMDispProgramRecordingLiteralValue:
                switch (v->literal.kind) {
                    case MVM_CALLSITE_ARG_OBJ:
                        fprintf(stderr, "    %d Literal object of type %s\n", i,
                                v->literal.value.o->st->debug_name);
                        break;
                    case MVM_CALLSITE_ARG_INT:
                        fprintf(stderr, "    %d Literal int value %"PRId64"\n", i,
                                v->literal.value.i64);
                        break;
                    case MVM_CALLSITE_ARG_NUM:
                        fprintf(stderr, "    %d Literal num value %g\n", i,
                                v->literal.value.n64);
                        break;
                    case MVM_CALLSITE_ARG_STR:
                        fprintf(stderr, "    %d Literal str value\n", i);
                        break;
                    default:
                        fprintf(stderr, "    %d Literal value of unknown kind\n", i);
                }
                break;
            case MVMDispProgramRecordingAttributeValue:
                fprintf(stderr, "    %d Attribute value\n", i);
                break;
            default:
                fprintf(stderr, "    %d Unknown\n", i);
        }
        if (v->guard_literal) {
            fprintf(stderr, "      Guard literal value\n");
        }
        else {
            if (v->guard_type && v->guard_concreteness)
                fprintf(stderr, "      Guard type and concreteness\n");
            else if (v->guard_type)
                fprintf(stderr, "      Guard type\n");
            else if (v->guard_concreteness)
                fprintf(stderr, "      Guard concreteness\n");
        }
    }
};
static void dump_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    fprintf(stderr, "Dispatch recording\n");
    fprintf(stderr, "  Captures:\n");
    dump_recording_capture(tc, &(record->rec.initial_capture), 4);
    fprintf(stderr, "  Values:\n");
    dump_recording_values(tc, &(record->rec));
    fprintf(stderr, "  Outcome:\n");
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_VALUE:
            printf("    Value\n");
            break;
        case MVM_DISP_OUTCOME_BYTECODE:
            printf("    Run bytecode\n");
            break;
        case MVM_DISP_OUTCOME_CFUNCTION:
            printf("    Run C function\n");
            break;
        default:
            printf("    Unknown\n");
    }
}
#else
#define dump_recording(tc, r) do {} while (0)
#endif

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
    record->rec.initial_capture.capture = capture;
    record->rec.initial_capture.transformation = MVMDispProgramRecordingInitial;
    MVM_VECTOR_INIT(record->rec.initial_capture.captures, 8);
    MVM_VECTOR_INIT(record->rec.values, 16);
    run_dispatch(tc, record, disp, capture, NULL);
}

/* Calculates the path to a capture. If the capture is not found, then an
 * exception will be thrown. The caller should pass in a pointer to a
 * CapturePath, which will be populated with the path to that capture. */
typedef struct {
    MVM_VECTOR_DECL(MVMDispProgramRecordingCapture *, path);
} CapturePath;
static MVMuint32 find_capture(MVMThreadContext *tc, MVMDispProgramRecordingCapture *current,
        MVMObject *searchee, CapturePath *p) {
    MVM_VECTOR_PUSH(p->path, current);
    if (current->capture == searchee)
        return 1;
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(current->captures); i++)
        if (find_capture(tc, &(current->captures[i]), searchee, p))
            return 1;
    MVM_VECTOR_POP(p->path);
    return 0;
}
static void calculate_capture_path(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMObject *capture, CapturePath *p) {
    if (!find_capture(tc, &(record->rec.initial_capture), capture, p)) {
        MVM_VECTOR_DESTROY(p->path);
        MVM_exception_throw_adhoc(tc,
                "Can only use manipulate a capture known in this dispatch");
    }
}
static void ensure_known_capture(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMObject *capture) {
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);
    MVM_VECTOR_DESTROY(p.path);
}

/* Ensures we have a constant recorded as a value. If there already is such
 * an entry, we re-use it and return its index. If not, we add a value entry. */
static MVMuint32 value_index_constant(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMCallsiteFlags kind, MVMRegister value) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingLiteralValue && v->literal.kind == kind) {
            switch (kind) {
                case MVM_CALLSITE_ARG_OBJ:
                    if (v->literal.value.o == value.o)
                        return i;
                case MVM_CALLSITE_ARG_INT:
                    if (v->literal.value.i64 == value.i64)
                        return i;
                case MVM_CALLSITE_ARG_NUM:
                    if (v->literal.value.n64 == value.n64)
                        return i;
                case MVM_CALLSITE_ARG_STR:
                    if (v->literal.value.s == value.s)
                        return i;
                default:
                    MVM_oops(tc, "Unknown kind of literal value in dispatch constant");
            }
        }
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingLiteralValue;
    new_value.literal.kind = kind;
    new_value.literal.value = value;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the specified argument index. */
static MVMuint32 value_index_capture(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 index) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingCaptureValue && v->capture.index == index)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingCaptureValue;
    new_value.capture.index = index;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Resolves a tracked value to a value index, throwing if it's not found. */
static MVMuint32 find_tracked_value_index(MVMThreadContext *tc,
        MVMDispProgramRecording *rec, MVMObject *tracked) {
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++)
        if (rec->values[i].tracked == tracked)
            return i;
    MVM_exception_throw_adhoc(tc, "Dispatcher tracked value not found");
}

/* Start tracking an argument from the specified capture. This allows us to
 * apply guards against it. */
MVMObject * MVM_disp_program_record_track_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 index) {
    /* Obtain the value from the capture. This ensures that it is in range. */
    MVMRegister value;
    MVMCallsiteFlags kind;
    MVM_capture_arg_pos(tc, capture, index, &value, &kind);

    /* Ensure the incoming capture is known. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Walk the capture path to resolve the index. We start at the deepest
     * point and work upward in the tree. */
    MVMint32 i;
    MVMuint32 real_index = index;
    MVMint32 found_value_index = -1;
    for (i = MVM_VECTOR_ELEMS(p.path) - 1; i >= 0; i--) {
        switch (p.path[i]->transformation) {
            case MVMDispProgramRecordingInsert:
                /* It's an insert. Was the insert at the index we are dealing
                 * with? */
                if (p.path[i]->index == real_index) {
                    /* Yes, and so it will have a value_index, which is what
                     * we ultimately need. */
                    found_value_index = p.path[i]->value_index;
                    break;
                }
                else {
                    /* No, so we may need to adjust the offset. Before this
                     * operation we had:
                     *   before1, before2, after1, after2
                     * And now have:
                     *   before1, before2, inserted, after1, after2
                     * Thus we need to adjust the current index by subtracting
                     * 1 if it's after the insertion point. */
                    if (real_index > p.path[i]->index)
                        real_index--;
                }
                break;
            case MVMDispProgramRecordingDrop:
                /* An argument was dropped here. Before the drop, we'd have
                 * had:
                 *   before1, before2, dropped, after1, after2
                 * And now we have:
                 *   before1, before2, after1, after2
                 * And so any index greater than or equal to the drop index
                 * needs to have 1 added to get its previous index. */
                if (real_index >= p.path[i]->index)
                    real_index++;
                break;
            case MVMDispProgramRecordingInitial:
                /* We have reached the initial capture, and so the index is
                 * for it. */
                break;
        }
    }
    MVM_VECTOR_DESTROY(p.path);

    /* If we didn't find a value index, then we're referencing the original
     * capture; ensure there's a value index for that. */
    if (found_value_index < 0)
        found_value_index = value_index_capture(tc, &(record->rec), real_index);

    /* Ensure there is a tracked value object and return it. */
    if (!record->rec.values[found_value_index].tracked)
        record->rec.values[found_value_index].tracked = MVM_tracked_create(tc, value, kind);
    return record->rec.values[found_value_index].tracked;
}

/* Record a guard of the current type of the specified tracked value. */
void MVM_disp_program_record_guard_type(MVMThreadContext *tc, MVMObject *tracked) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    record->rec.values[value_index].guard_type = 1;
}

/* Record a guard of the current concreteness of the specified tracked value. */
void MVM_disp_program_record_guard_concreteness(MVMThreadContext *tc, MVMObject *tracked) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    record->rec.values[value_index].guard_concreteness = 1;
}

/* Record a guard of the current value of the specified tracked value. */
void MVM_disp_program_record_guard_literal(MVMThreadContext *tc, MVMObject *tracked) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    record->rec.values[value_index].guard_literal = 1;
}

/* Record a guard that the specified tracked value must not be a certain object
 * literal. */
void MVM_disp_program_record_guard_not_literal_obj(MVMThreadContext *tc,
       MVMObject *tracked, MVMObject *object) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    // TODO
    MVM_oops(tc, "not literal object guards NYI");
}

/* Record that we drop an argument from a capture. Also perform the drop,
 * resulting in a new capture without that argument. */
MVMObject * MVM_disp_program_record_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 idx) {
    /* Lookup the path to the incoming capture. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Calculate the new capture and add a record for it. */
    MVMObject *new_capture = MVM_capture_drop_arg(tc, capture, idx);
    MVMDispProgramRecordingCapture new_capture_record = {
        .capture = new_capture,
        .transformation = MVMDispProgramRecordingDrop,
        .index = idx
    };
    MVM_VECTOR_INIT(new_capture_record.captures, 0);
    MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, new_capture_record);
    MVM_VECTOR_DESTROY(p.path);

    /* Evaluate to the new capture, for the running dispatch function. */
    return new_capture;
}

/* Record that we insert a tracked value into a capture. Also perform the insert
 * on the value that was read. */
MVMObject * MVM_disp_program_record_capture_insert_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 idx, MVMObject *tracked) {
    /* Lookup the index of the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);

    /* Also look up the path to the incoming capture. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Calculate the new capture and add a record for it. */
    MVMObject *new_capture = MVM_capture_insert_arg(tc, capture, idx,
            ((MVMTracked *)tracked)->body.kind, ((MVMTracked *)tracked)->body.value);
    MVMDispProgramRecordingCapture new_capture_record = {
        .capture = new_capture,
        .transformation = MVMDispProgramRecordingInsert,
        .index = idx,
        .value_index = value_index
    };
    MVM_VECTOR_INIT(new_capture_record.captures, 0);
    MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, new_capture_record);
    MVM_VECTOR_DESTROY(p.path);

    /* Evaluate to the new capture, for the running dispatch function. */
    return new_capture;
}

/* Record that we insert a new constant argument from a capture. Also perform the
 * insert, resulting in a new capture without a new argument inserted at the
 * given index. */
MVMObject * MVM_disp_program_record_capture_insert_constant_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 idx, MVMCallsiteFlags kind, MVMRegister value) {
    /* Lookup the path to the incoming capture. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Obtain a new value index for the constant. */
    MVMuint32 value_index = value_index_constant(tc, &(record->rec), kind, value);

    /* Calculate the new capture and add a record for it. */
    MVMObject *new_capture = MVM_capture_insert_arg(tc, capture, idx, kind, value);
    MVMDispProgramRecordingCapture new_capture_record = {
        .capture = new_capture,
        .transformation = MVMDispProgramRecordingInsert,
        .index = idx,
        .value_index = value_index
    };
    MVM_VECTOR_INIT(new_capture_record.captures, 0);
    MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, new_capture_record);
    MVM_VECTOR_DESTROY(p.path);

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

/* Processing the recorded program. */
static void process_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord* record) {
    dump_recording(tc, record);
}

/* Called when we have finished recording a dispatch program. */
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record,
        MVMuint32 *thunked) {
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
            process_recording(tc, record);
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
            process_recording(tc, record);
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
            MVM_frame_dispatch(tc, record->outcome.code, record->outcome.args, -1);
            if (thunked)
                *thunked = 1;
            return 0;
        case MVM_DISP_OUTCOME_CFUNCTION:
            process_recording(tc, record);
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = MVM_callstack_record_to_frame(tc->stack_top->prev);
            record->outcome.c_func(tc, record->outcome.args);
            return 1;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}
