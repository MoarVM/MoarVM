#include "moar.h"

/* Debug dumping, to figure out what we're recording and what programs we are
 * inferring from those recordings. */
#define DUMP_RECORDINGS     0
#define DUMP_PROGRAMS       0

#if DUMP_RECORDINGS
static void dump_recording_capture(MVMThreadContext *tc,
        MVMDispProgramRecordingCapture *capture, MVMuint32 indent,
        MVMObject *outcome) {
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
    if (capture->capture == outcome)
        fprintf(stderr, "%s  Used as args for invoke result\n", indent_str);
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(capture->captures); i++)
        dump_recording_capture(tc, &(capture->captures[i]), indent + 2, outcome);
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
    dump_recording_capture(tc, &(record->rec.initial_capture), 4, record->rec.outcome_capture);
    fprintf(stderr, "  Values:\n");
    dump_recording_values(tc, &(record->rec));
    fprintf(stderr, "  Outcome:\n");
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_VALUE:
            printf("    Value %d\n", record->rec.outcome_value);
            break;
        case MVM_DISP_OUTCOME_BYTECODE:
            printf("    Run bytecode of value %d\n", record->rec.outcome_value);
            break;
        case MVM_DISP_OUTCOME_CFUNCTION:
            printf("    Run C function of value %d\n", record->rec.outcome_value);
            break;
        default:
            printf("    Unknown\n");
    }
}
#else
#define dump_recording(tc, r) do {} while (0)
#endif

#if DUMP_PROGRAMS
static void dump_program(MVMThreadContext *tc, MVMDispProgram *dp) {
    fprintf(stderr, "Dispatch program\n");
    MVMuint32 i;
    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            case MVMDispOpcodeGuardArgType:
                fprintf(stderr, "  Guard arg %d (type=%s)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardArgTypeConc:
                fprintf(stderr, "  Guard arg %d (type=%s, concrete)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
           case MVMDispOpcodeGuardArgTypeTypeObject:
                fprintf(stderr, "  Guard arg %d (type=%s, type object)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardArgConc:
                fprintf(stderr, "  Guard arg %d (concrete)\n",
                        op->arg_guard.arg_idx);
                break;
            case MVMDispOpcodeGuardArgTypeObject:
                fprintf(stderr, "  Guard arg %d (type object)\n",
                        op->arg_guard.arg_idx);
                break;
            case MVMDispOpcodeGuardArgLiteralObj:
                fprintf(stderr, "  Guard arg %d (literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardArgLiteralStr: {
                char *c_str = MVM_string_utf8_encode_C_string(tc, 
                        ((MVMString *)dp->gc_constants[op->arg_guard.checkee]));
                fprintf(stderr, "  Guard arg %d (literal string '%s')\n",
                        op->arg_guard.arg_idx, c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeGuardArgLiteralInt:
                fprintf(stderr, "  Guard arg %d (literal integer %"PRIi64")\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].i64);
                break;
            case MVMDispOpcodeGuardArgLiteralNum:
                fprintf(stderr, "  Guard arg %d (literal number %g)\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].n64);
                break;
            case MVMDispOpcodeGuardArgNotLiteralObj:
                fprintf(stderr, "  Guard arg %d (not literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                break;
            default:
                fprintf(stderr, "  UNKNOWN OP %d\n", op->code);
        }
    }
}
#else
#define dump_program(tc, dp) do {} while (0)
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
    record->rec.outcome_capture = NULL;
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
                    break;
                case MVM_CALLSITE_ARG_INT:
                    if (v->literal.value.i64 == value.i64)
                        return i;
                    break;
                case MVM_CALLSITE_ARG_NUM:
                    if (v->literal.value.n64 == value.n64)
                        return i;
                    break;
                case MVM_CALLSITE_ARG_STR:
                    if (v->literal.value.s == value.s)
                        return i;
                    break;
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
    MVMRegister value = { .o = result };
    record->rec.outcome_value = value_index_constant(tc, &(record->rec),
            MVM_CALLSITE_ARG_OBJ, value);

    /* Put the return value in place. */
    record->outcome.kind = MVM_DISP_OUTCOME_VALUE;
    record->outcome.result_value = value;
    record->outcome.result_kind = MVM_reg_obj;
}

/* Record a program terminator that reads the value from an argument capture. */
void MVM_disp_program_record_result_tracked_value(MVMThreadContext *tc, MVMObject *tracked) {
    /* Look up the tracked value and note it as the outcome value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    record->rec.outcome_value = value_index;

    /* Put the return value in place. */
    record->outcome.kind = MVM_DISP_OUTCOME_VALUE;
    record->outcome.result_value = ((MVMTracked *)tracked)->body.value;
    switch (((MVMTracked *)tracked)->body.kind) {
        case MVM_CALLSITE_ARG_OBJ: record->outcome.result_kind = MVM_reg_obj; break;
        case MVM_CALLSITE_ARG_INT: record->outcome.result_kind = MVM_reg_int64; break;
        case MVM_CALLSITE_ARG_NUM: record->outcome.result_kind = MVM_reg_num64; break;
        case MVM_CALLSITE_ARG_STR: record->outcome.result_kind = MVM_reg_str; break;
        default: MVM_oops(tc, "Unknown capture value type in boot-value dispatch");
    }
}

/* Record a program terminator that invokes an MVMCode object, which is to be
 * considered a constant (e.g. so long as the guards that come before this
 * point match, the thing to invoke is always this code object). */
void MVM_disp_program_record_code_constant(MVMThreadContext *tc, MVMCode *result, MVMObject *capture) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);
    MVMRegister value = { .o = (MVMObject *)result };
    record->rec.outcome_value = value_index_constant(tc, &(record->rec),
            MVM_CALLSITE_ARG_OBJ, value);
    record->rec.outcome_capture = capture;

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
    MVMRegister value = { .o = (MVMObject *)result };
    record->rec.outcome_value = value_index_constant(tc, &(record->rec),
            MVM_CALLSITE_ARG_OBJ, value);
    record->rec.outcome_capture = capture;

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_CFUNCTION;
    record->outcome.c_func = result->body.func;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Process a recorded program. */
typedef struct {
    MVMDispProgramRecording *rec;
    MVM_VECTOR_DECL(MVMCollectable *, gc_constants);
    MVM_VECTOR_DECL(MVMDispProgramConstant, constants);
    MVM_VECTOR_DECL(MVMDispProgramOp, ops);
} compile_state;
static MVMuint32 add_program_constant_int(MVMThreadContext *tc, compile_state *cs,
        MVMint64 value) {
    MVMDispProgramConstant c = { .i64 = value };
    MVM_VECTOR_PUSH(cs->constants, c);
    return MVM_VECTOR_ELEMS(cs->constants) - 1;
}
static MVMuint32 add_program_constant_num(MVMThreadContext *tc, compile_state *cs,
        MVMnum64 value) {
    MVMDispProgramConstant c = { .n64 = value };
    MVM_VECTOR_PUSH(cs->constants, c);
    return MVM_VECTOR_ELEMS(cs->constants) - 1;
}
static MVMuint32 add_program_gc_constant(MVMThreadContext *tc, compile_state *cs,
        MVMCollectable *value) {
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(cs->gc_constants); i++)
        if (cs->gc_constants[i] == value)
            return i;
    MVM_VECTOR_PUSH(cs->gc_constants, value);
    return MVM_VECTOR_ELEMS(cs->gc_constants) - 1;
}
static MVMuint32 add_program_constant_obj(MVMThreadContext *tc, compile_state *cs,
        MVMObject *value) {
    return add_program_gc_constant(tc, cs, (MVMCollectable *)value);
}
static MVMuint32 add_program_constant_str(MVMThreadContext *tc, compile_state *cs,
        MVMString *value) {
    return add_program_gc_constant(tc, cs, (MVMCollectable *)value);
}
static MVMuint32 add_program_constant_stable(MVMThreadContext *tc, compile_state *cs,
        MVMSTable *value) {
    return add_program_gc_constant(tc, cs, (MVMCollectable *)value);
}
static void emit_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v) {
    /* Fetch the callsite entry flags. In the easiest possible case, it's a
     * non-object constant, so no guards. In theory, we can do better at
     * objects if we can prove they come from a wval, but the bytecode
     * validation can't yet do that, and we have a safety problem if we
     * just take the bytecode file at its word. */
    MVMObject *capture_obj = cs->rec->initial_capture.capture;
    MVMCapture *initial_capture = (MVMCapture *)capture_obj;
    MVMuint32 index = v->capture.index;
    MVMCallsiteFlags cs_flag = initial_capture->body.callsite->arg_flags[index];
    if ((cs_flag & MVM_CALLSITE_ARG_LITERAL) && !(cs_flag & MVM_CALLSITE_ARG_OBJ))
        return;

    /* Otherwise, go by argument kind. For objects, all kinds of guards
     * are possible. For others, only the literal value ones are. */
    switch (cs_flag & MVM_CALLSITE_ARG_TYPE_MASK) {
        case MVM_CALLSITE_ARG_OBJ:
            if (v->guard_literal) {
                /* If we're guarding that it's a literal, we can disregard
                 * the other kinds of guard, since this one implies both
                 * type and concreteness. */
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardArgLiteralObj;
                op.arg_guard.arg_idx = (MVMuint16)index;
                op.arg_guard.checkee = add_program_constant_obj(tc, cs,
                        MVM_capture_arg_pos_o(tc, capture_obj, index));
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            else {
                if (v->guard_type) {
                    MVMObject *value = MVM_capture_arg_pos_o(tc, capture_obj, index);
                    MVMDispProgramOp op;
                    if (v->guard_concreteness)
                        op.code = IS_CONCRETE(value)
                                ? MVMDispOpcodeGuardArgTypeConc
                                : MVMDispOpcodeGuardArgTypeTypeObject;
                    else
                        op.code = MVMDispOpcodeGuardArgType;
                    op.arg_guard.arg_idx = (MVMuint16)index;
                    op.arg_guard.checkee = add_program_constant_stable(tc, cs,
                            STABLE(value));
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
                else if (v->guard_concreteness) {
                    MVMObject *value = MVM_capture_arg_pos_o(tc, capture_obj, index);
                    MVMDispProgramOp op;
                    op.code = IS_CONCRETE(value)
                            ? MVMDispOpcodeGuardArgConc
                            : MVMDispOpcodeGuardArgTypeObject;
                    op.arg_guard.arg_idx = (MVMuint16)index;
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
                MVMuint32 i;
                for (i = 0; i < MVM_VECTOR_ELEMS(v->not_literal_guards); i++) {
                    MVMDispProgramOp op;
                    op.code = MVMDispOpcodeGuardArgNotLiteralObj;
                    op.arg_guard.arg_idx = (MVMuint16)index;
                    op.arg_guard.checkee = add_program_constant_obj(tc, cs,
                            v->not_literal_guards[i]);
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
            }
            break;
        case MVM_CALLSITE_ARG_STR:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardArgLiteralStr;
                op.arg_guard.arg_idx = (MVMuint16)index;
                op.arg_guard.checkee = add_program_constant_str(tc, cs,
                        MVM_capture_arg_pos_s(tc, capture_obj, index));
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        case MVM_CALLSITE_ARG_INT:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardArgLiteralInt;
                op.arg_guard.arg_idx = (MVMuint16)index;
                op.arg_guard.checkee = add_program_constant_int(tc, cs,
                        MVM_capture_arg_pos_i(tc, capture_obj, index));
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        case MVM_CALLSITE_ARG_NUM:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardArgLiteralInt;
                op.arg_guard.arg_idx = (MVMuint16)index;
                op.arg_guard.checkee = add_program_constant_num(tc, cs,
                        MVM_capture_arg_pos_n(tc, capture_obj, index));
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        default:
            MVM_oops(tc, "Unexpected callsite arg type in emit_guards");
    }
}
static void process_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord* record) {
    /* Dump the recording if we're debugging. */
    dump_recording(tc, record);

    /* Go through the values and compile the guards associated with them,
     * along with any loads. */
    compile_state cs;
    cs.rec = &(record->rec);
    MVM_VECTOR_INIT(cs.ops, 8);
    MVM_VECTOR_INIT(cs.gc_constants, 4);
    MVM_VECTOR_INIT(cs.constants, 4);
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(record->rec.values); i++) {
        MVMDispProgramRecordingValue *v = &(record->rec.values[i]);
        if (v->source == MVMDispProgramRecordingCaptureValue) {
            emit_guards(tc, &cs, v);
        }
    }

    // TODO outcome, capture, temporaries, etc.

    /* Create dispatch program description. */
    MVMDispProgram *dp = MVM_malloc(sizeof(MVMDispProgram));
    dp->constants = cs.constants;
    dp->gc_constants = cs.gc_constants;
    dp->ops = cs.ops;
    dp->num_ops = MVM_VECTOR_ELEMS(cs.ops);

    /* Dump the program if we're debugging. */
    dump_program(tc, dp);
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

#define GET_ARG MVMRegister val = args->source[args->map[op->arg_guard.arg_idx]]

MVMint64 MVM_disp_program_run(MVMThreadContext *tc, MVMDispProgram *dp, MVMArgs *args) {
    MVMuint32 i;

    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);

        switch (op->code) {
            case MVMDispOpcodeGuardArgType: {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->arg_guard.checkee])
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgTypeConc: {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->arg_guard.checkee]
                        || !IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgTypeTypeObject: {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->arg_guard.checkee]
                        || IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgConc: {
                GET_ARG;
                if (!IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgTypeObject: {
                GET_ARG;
                if (IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgLiteralObj: {
                GET_ARG;
                if (val.o != (MVMObject *)dp->gc_constants[op->arg_guard.checkee])
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgLiteralStr: {
                GET_ARG;
                if (!MVM_string_equal(tc, val.s, (MVMString *)dp->gc_constants[op->arg_guard.checkee]))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgLiteralInt: {
                GET_ARG;
                if (val.i64 != dp->constants[op->arg_guard.checkee].i64)
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgLiteralNum: {
                GET_ARG;
                if (val.n64 != dp->constants[op->arg_guard.checkee].n64)
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardArgNotLiteralObj: {
                GET_ARG;
                if (val.o == (MVMObject *)dp->gc_constants[op->arg_guard.checkee])
                    goto rejection;
                break;
            }
            default:
                fprintf(stderr, "  UNKNOWN OP %d\n", op->code);
        }
    }

    return 1;
rejection:
    return 0;
}
