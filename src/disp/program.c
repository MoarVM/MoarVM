#include "moar.h"

/* Debug dumping, to figure out what we're recording and what programs we are
 * inferring from those recordings. */
#define DUMP_RECORDINGS     0
#define DUMP_PROGRAMS       0

#if DUMP_RECORDINGS
static void dump_recording_capture(MVMThreadContext *tc,
        MVMDispProgramRecordingCapture *capture, MVMuint32 indent,
        MVMDispProgramRecording *rec) {
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
    if (capture->capture == rec->outcome_capture)
        fprintf(stderr, "%s  Used as args for invoke result\n", indent_str);
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->resume_inits); i++) {
        if (rec->resume_inits[i].capture == capture->capture) {
            char *disp_id = MVM_string_utf8_encode_C_string(tc, rec->resume_inits[i].disp->id);
            fprintf(stderr, "%s  Used as resume init args for %s\n", indent_str, disp_id);
            MVM_free(disp_id);
        }
    }
    for (i = 0; i < MVM_VECTOR_ELEMS(capture->captures); i++)
        dump_recording_capture(tc, &(capture->captures[i]), indent + 2, rec);
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
                fprintf(stderr, "    %d Attribute value from offset %d of value %d \n", i,
                        v->attribute.offset, v->attribute.from_value);
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
    dump_recording_capture(tc, &(record->rec.initial_capture), 4, &(record->rec));
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
    if (dp->first_args_temporary == dp->num_temporaries)
        fprintf(stderr, "Dispatch program (%d temporaries)\n", dp->num_temporaries);
    else
        fprintf(stderr, "Dispatch program (%d temporaries, args from %d)\n",
                dp->num_temporaries, dp->first_args_temporary);
    MVMuint32 i;
    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            /* Opcodes that guard on values in argument slots */
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

            /* Opcodes that guard on values in temporaries */
            case MVMDispOpcodeGuardTempType:
                fprintf(stderr, "  Guard temp %d (type=%s)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardTempTypeConc:
                fprintf(stderr, "  Guard temp %d (type=%s, concrete)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
           case MVMDispOpcodeGuardTempTypeTypeObject:
                fprintf(stderr, "  Guard temp %d (type=%s, type object)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardTempConc:
                fprintf(stderr, "  Guard temp %d (concrete)\n",
                        op->temp_guard.temp);
                break;
            case MVMDispOpcodeGuardTempTypeObject:
                fprintf(stderr, "  Guard temp %d (type object)\n",
                        op->temp_guard.temp);
                break;
            case MVMDispOpcodeGuardTempLiteralObj:
                fprintf(stderr, "  Guard temp %d (literal object of type %s)\n",
                        op->temp_guard.temp,
                        STABLE(((MVMObject *)dp->gc_constants[op->temp_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardTempLiteralStr: {
                char *c_str = MVM_string_utf8_encode_C_string(tc, 
                        ((MVMString *)dp->gc_constants[op->temp_guard.checkee]));
                fprintf(stderr, "  Guard temp %d (literal string '%s')\n",
                        op->temp_guard.temp, c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeGuardTempLiteralInt:
                fprintf(stderr, "  Guard temp %d (literal integer %"PRIi64")\n",
                        op->temp_guard.temp,
                        dp->constants[op->temp_guard.checkee].i64);
                break;
            case MVMDispOpcodeGuardTempLiteralNum:
                fprintf(stderr, "  Guard temp %d (literal number %g)\n",
                        op->temp_guard.temp,
                        dp->constants[op->temp_guard.checkee].n64);
                break;
            case MVMDispOpcodeGuardTempNotLiteralObj:
                fprintf(stderr, "  Guard temp %d (not literal object of type %s)\n",
                        op->temp_guard.temp,
                        STABLE(((MVMObject *)dp->gc_constants[op->temp_guard.checkee]))->debug_name);
                break;

            /* Opcodes that load values into temporaries. */
            case MVMDispOpcodeLoadCaptureValue:
                fprintf(stderr, "  Load argument %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantObjOrStr:
                fprintf(stderr, "  Load collectable constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantInt:
                fprintf(stderr, "  Load integer constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantNum:
                fprintf(stderr, "  Load number constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeObj:
                fprintf(stderr, "  Deference object attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeInt:
                fprintf(stderr, "  Deference integer attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeNum:
                fprintf(stderr, "  Deference number attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeStr:
                fprintf(stderr, "  Deference string attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeSet:
                fprintf(stderr, "  Copy temporary %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;

            /* Opcodes that set a result value */
            case MVMDispOpcodeResultValueObj:
                fprintf(stderr, "  Set result object value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueStr:
                fprintf(stderr, "  Set result string value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueInt:
                fprintf(stderr, "  Set result integer value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueNum:
                fprintf(stderr, "  Set result num value from temporary %d\n",
                        op->res_value.temp);
                break;

            /* Opcodes that handle invocation results. */
            case MVMDispOpcodeUseArgsTail:
                fprintf(stderr, "  Skip first %d args of incoming capture; callsite from %d\n",
                        op->use_arg_tail.skip_args, op->use_arg_tail.callsite_idx);
                break;
            case MVMDispOpcodeCopyArgsTail:
                fprintf(stderr, "  Copy final %d args of incoming capture; callsite from %d\n",
                        op->copy_arg_tail.tail_args, op->copy_arg_tail.callsite_idx);
                break;
            case MVMDispOpcodeResultBytecode:
                fprintf(stderr, "  Invoke MVMCode in temporary %d\n",
                        op->res_code.temp_invokee);
                break;
            case MVMDispOpcodeResultCFunction:
                fprintf(stderr, "  Invoke MVMCFunction in temporary %d\n",
                        op->res_code.temp_invokee);
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
static MVMFrame * find_calling_frame(MVMCallStackRecord *prev) {
    /* Typically, we'll have the frame right off, but if there was flattening,
     * we need to skip that frame between the two. */
    while (prev->kind == MVM_CALLSTACK_RECORD_FLATTENING ||
            prev->kind == MVM_CALLSTACK_RECORD_START_REGION)
        prev = prev->prev;
    return MVM_callstack_record_to_frame(prev);
}
static void run_dispatch(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMDispDefinition *disp, MVMObject *capture, MVMuint32 *thunked) {
    MVMCallsite *disp_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
    record->current_disp = disp;
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
        MVM_callstack_unwind_dispatch_record(tc, thunked);
    }
    else if (REPR(dispatch)->ID == MVM_REPR_ID_MVMCode) {
        record->outcome.kind = MVM_DISP_OUTCOME_EXPECT_DELEGATE;
        record->outcome.delegate_disp = NULL;
        record->outcome.delegate_capture = NULL;
        tc->cur_frame = find_calling_frame(tc->stack_top->prev);
        MVM_frame_dispatch(tc, (MVMCode *)dispatch, dispatch_args, -1);
        if (thunked)
            *thunked = 1;
    }
    else {
        MVM_panic(1, "dispatch callback only supported as a MVMCFunction or MVMCode");
    }
}
void MVM_disp_program_run_dispatch(MVMThreadContext *tc, MVMDispDefinition *disp,
        MVMObject *capture, MVMDispInlineCacheEntry **ic_entry_ptr,
        MVMDispInlineCacheEntry *ic_entry, MVMStaticFrame *update_sf) {
    /* Push a dispatch recording frame onto the callstack; this is how we'll
     * keep track of the current recording state. */
    MVMCallStackDispatchRecord *record = MVM_callstack_allocate_dispatch_record(tc);
    record->rec.initial_capture.capture = capture;
    record->rec.initial_capture.transformation = MVMDispProgramRecordingInitial;
    record->rec.resume_kind = MVMDispProgramRecordingResumeNone;
    MVM_VECTOR_INIT(record->rec.initial_capture.captures, 8);
    MVM_VECTOR_INIT(record->rec.values, 16);
    MVM_VECTOR_INIT(record->rec.resume_inits, 4);
    record->rec.outcome_capture = NULL;
    record->ic_entry_ptr = ic_entry_ptr;
    record->ic_entry = ic_entry;
    record->update_sf = update_sf;
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

/* Ensures we have a values used entry for the specified attribute read. */
static MVMuint32 value_index_attribute(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 from_value, MVMuint32 offset, MVMCallsiteFlags kind) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingAttributeValue &&
                v->attribute.from_value == from_value &&
                v->attribute.offset == offset &&
                v->attribute.kind == kind)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingAttributeValue;
    new_value.attribute.from_value = from_value;
    new_value.attribute.offset = offset;
    new_value.attribute.kind = kind;
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

/* Start tracking an attribute read against the given tracked object. This
 * lets us read the attribute in the dispatch program and use it in a result
 * capture, for example. */
MVMObject * MVM_disp_program_record_track_attr(MVMThreadContext *tc, MVMObject *tracked_in,
        MVMObject *class_handle, MVMString *name) {
    /* Ensure the tracked value is an object type. */
    if (((MVMTracked *)tracked_in)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-attr on a tracked object");
    
    /* Resolve the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_in);

    /* Obtain the object and ensure it is a concrete P6opaque; also track its
     * type and concreteness since attr read safety depends on this. */
    MVMObject *read_from = ((MVMTracked *)tracked_in)->body.value.o;
    if (REPR(read_from)->ID != MVM_REPR_ID_P6opaque)
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-attr on a P6opaque");
    if (!IS_CONCRETE(read_from))
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-attr on a concrete object");
    record->rec.values[value_index].guard_type = 1;
    record->rec.values[value_index].guard_concreteness = 1;

    /* Work out the index that the value lives at, along with its kind, and
     * then read the value. */
    size_t offset;
    MVMCallsiteFlags attr_kind;
    MVM_p6opaque_attr_offset_and_arg_type(tc, read_from, class_handle, name, &offset, &attr_kind);
    MVMRegister attr_value;
    switch (attr_kind) {
        case MVM_CALLSITE_ARG_OBJ:
            attr_value.o = MVM_p6opaque_read_object(tc, read_from, offset);
            if (attr_value.o == NULL) /* Hopefully this can be an error eventually */
                attr_value.o = tc->instance->VMNull;
            break;
        case MVM_CALLSITE_ARG_INT:
            attr_value.i64 = MVM_p6opaque_read_int64(tc, read_from, offset);
            break;
        case MVM_CALLSITE_ARG_NUM:
            attr_value.n64 = MVM_p6opaque_read_num64(tc, read_from, offset);
            break;
        case MVM_CALLSITE_ARG_STR:
            attr_value.s = MVM_p6opaque_read_str(tc, read_from, offset);
            break;
        default:
            MVM_oops(tc, "Unhandled attribute kind when trying to track attribute");
    }

    /* Ensure that we have this attribute read in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_attribute(tc, &(record->rec),
            value_index, (MVMuint32)offset, attr_kind);
    if (!record->rec.values[result_value_index].tracked)
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
                attr_value, attr_kind);
    return record->rec.values[result_value_index].tracked;
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
    MVM_VECTOR_PUSH(record->rec.values[value_index].not_literal_guards, object);
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

/* Record the setting of the dispatch resume init args (the arguments that
 * should be made available for initializing resumption). */
void MVM_disp_program_record_set_resume_init_args(MVMThreadContext *tc, MVMObject *capture) {
    /* Make sure we're in a resumable dispatcher and that the capture is
     * tracked. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (!record->current_disp->resume)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-set-resume-init-args in a resumable dispatcher");
    ensure_known_capture(tc, record, capture);

    /* Record the saving of the resume init state for this dispatcher, making
     * sure we didn't already do so. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(record->rec.resume_inits); i++)
        if (record->rec.resume_inits[i].disp == record->current_disp)
            MVM_exception_throw_adhoc(tc, "Already set resume init args for this dispatcher");
    MVMDispProgramRecordingResumeInit new_resume_init;
    new_resume_init.disp = record->current_disp;
    new_resume_init.capture = capture;
    MVM_VECTOR_PUSH(record->rec.resume_inits, new_resume_init);
}

/* Record the getting of the dispatch resume init args. */
MVMObject * MVM_disp_program_record_get_resume_init_args(MVMThreadContext *tc) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-get-resume-init-args in a resume callback");
    MVM_panic(1, "get resume init args nyi");
}

/* Ensure we're in a state where running the resume dispatcher is OK. */
static void ensure_resume_ok(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    if (record->rec.resume_kind != MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc, "Can only enter a resumption once in a dispatch");
}

/* Record the resumption of a dispatch. */
void MVM_disp_program_record_resume(MVMThreadContext *tc, MVMObject *capture) {
    /* Make sure we're in a dispatcher and that we didn't already call resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_resume_ok(tc, record);
    ensure_known_capture(tc, record, capture);

    /* Find the dispatch record we're going to be resuming. */

    /* Record the kind of dispatch resumption we're doing, and then delegate to
     * the appropriate `resume` dispatcher callback. */
    record->rec.resume_kind = MVMDispProgramRecordingResumeTopmost;
    MVM_panic(1, "record resume nyi");
}

/* Record the resumption of a dispatch found relative to our caller. */
void MVM_disp_program_record_resume_caller(MVMThreadContext *tc, MVMObject *capture) {
    /* Make sure we're in a dispatcher and that we didn't already call resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_resume_ok(tc, record);
    ensure_known_capture(tc, record, capture);

    /* Find the dispatch record we're going to be resuming. */

    /* Record the kind of dispatch resumption we're doing, and then delegate to
     * the appropriate `resume` dispatcher callback. */
    record->rec.resume_kind = MVMDispProgramRecordingResumeCaller;
    MVM_panic(1, "record resume caller nyi");
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

/* Record a program terminator that is a constant object value. */
void MVM_disp_program_record_result_constant(MVMThreadContext *tc, MVMCallsiteFlags kind,
        MVMRegister value) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    record->rec.outcome_value = value_index_constant(tc, &(record->rec), kind, value);

    /* Put the return value in place. */
    record->outcome.kind = MVM_DISP_OUTCOME_VALUE;
    record->outcome.result_value = value;
    switch (kind) {
        case MVM_CALLSITE_ARG_OBJ: record->outcome.result_kind = MVM_reg_obj; break;
        case MVM_CALLSITE_ARG_INT: record->outcome.result_kind = MVM_reg_int64; break;
        case MVM_CALLSITE_ARG_NUM: record->outcome.result_kind = MVM_reg_num64; break;
        case MVM_CALLSITE_ARG_STR: record->outcome.result_kind = MVM_reg_str; break;
        default: MVM_oops(tc, "Unknown capture value type in boot-constant dispatch");
    }
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
    /* A map of the temporaries, and which values they hold. This only covers
     * those involved in executing the guard program, not in building up a
     * final result capture. For now, we don't try and re-use temporaries. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingValue *, value_temps);
    /* If we need to have further temporaries for an args buffer. */
    MVMuint32 args_buffer_temps;
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
static MVMuint32 add_program_constant_callsite(MVMThreadContext *tc, compile_state *cs,
        MVMCallsite *value) {
    /* The callsite must be interned to be used in a dispatch program. */
    if (!value->is_interned)
        MVM_callsite_intern(tc, &value, 1, 0);
    MVMDispProgramConstant c = { .cs = value };
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
static MVMuint32 get_temp_holding_value(MVMThreadContext *tc, compile_state *cs,
        MVMuint32 value_index) {
    /* See if we already loaded it. */
    MVMuint32 i;
    MVMDispProgramRecordingValue *v = &(cs->rec->values[value_index]);
    for (i = 0; i < MVM_VECTOR_ELEMS(cs->value_temps); i++)
        if (cs->value_temps[i] == v)
            return i;

    /* Otherwise, we need to allocate a temporary and emit a load. */
    MVMDispProgramOp op;
    op.load.temp = MVM_VECTOR_ELEMS(cs->value_temps);
    MVM_VECTOR_PUSH(cs->value_temps, v);
    switch (v->source) {
        case MVMDispProgramRecordingCaptureValue:
            op.code = MVMDispOpcodeLoadCaptureValue;
            op.load.idx = v->capture.index;
            break;
        case MVMDispProgramRecordingLiteralValue:
            switch (v->literal.kind) {
                case MVM_CALLSITE_ARG_OBJ:
                    op.code = MVMDispOpcodeLoadConstantObjOrStr;
                    op.load.idx = add_program_constant_obj(tc, cs, v->literal.value.o);
                    break;
                case MVM_CALLSITE_ARG_STR:
                    op.code = MVMDispOpcodeLoadConstantObjOrStr;
                    op.load.idx = add_program_constant_str(tc, cs, v->literal.value.s);
                    break;
                case MVM_CALLSITE_ARG_INT:
                    op.code = MVMDispOpcodeLoadConstantInt;
                    op.load.idx = add_program_constant_int(tc, cs, v->literal.value.i64);
                    break;
                case MVM_CALLSITE_ARG_NUM:
                    op.code = MVMDispOpcodeLoadConstantNum;
                    op.load.idx = add_program_constant_num(tc, cs, v->literal.value.n64);
                    break;
                default:
                    MVM_oops(tc, "Unhandled kind of literal value in recorded dispatch");
            }
            break;
        case MVMDispProgramRecordingAttributeValue: {
            /* We first need to make sure that we load the dependent value (we
             * surely will have due to it needing to be guarded). */
            MVMuint32 from_temp = get_temp_holding_value(tc, cs, v->attribute.from_value);

            /* Reading an attribute happens in two steps. First we copy the from
             * value into the target temporary. */
            op.code = MVMDispOpcodeSet;
            op.load.idx = from_temp;
            MVM_VECTOR_PUSH(cs->ops, op);

            /* Then we do the attribute access. */
            switch (v->attribute.kind) {
                case MVM_CALLSITE_ARG_OBJ:
                    op.code = MVMDispOpcodeLoadAttributeObj;
                    break;
                case MVM_CALLSITE_ARG_STR:
                    op.code = MVMDispOpcodeLoadAttributeStr;
                    break;
                case MVM_CALLSITE_ARG_INT:
                    op.code = MVMDispOpcodeLoadAttributeInt;
                    break;
                case MVM_CALLSITE_ARG_NUM:
                    op.code = MVMDispOpcodeLoadAttributeNum;
                    break;
                default:
                    MVM_oops(tc, "Unhandled kind of literal value in recorded dispatch");
            }
            op.load.idx = v->attribute.offset;
            break;
        }
        default:
            MVM_oops(tc, "Did not yet implement temporary loading for this value source");
    }
    MVM_VECTOR_PUSH(cs->ops, op);
    return op.load.temp;
}
static void emit_capture_guards(MVMThreadContext *tc, compile_state *cs,
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
            MVM_oops(tc, "Unexpected callsite arg type in emit_capture_guards");
    }
}
static void emit_attribute_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v, MVMuint32 value_index) {
    /* Ensure the attribute is loaded. */
    MVMuint32 temp = get_temp_holding_value(tc, cs, value_index);
    MVMRegister value = ((MVMTracked *)v->tracked)->body.value;

    /* Now go by the kind of attribute. */
    switch (v->attribute.kind) {
        case MVM_CALLSITE_ARG_OBJ:
            if (v->guard_literal) {
                /* If we're guarding that it's a literal, we can disregard
                 * the other kinds of guard, since this one implies both
                 * type and concreteness. */
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardTempLiteralObj;
                op.temp_guard.temp = temp;
                op.temp_guard.checkee = add_program_constant_obj(tc, cs, value.o);
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            else {
                if (v->guard_type) {
                    MVMDispProgramOp op;
                    if (v->guard_concreteness)
                        op.code = IS_CONCRETE(value.o)
                                ? MVMDispOpcodeGuardTempTypeConc
                                : MVMDispOpcodeGuardTempTypeTypeObject;
                    else
                        op.code = MVMDispOpcodeGuardTempType;
                    op.temp_guard.temp = temp;
                    op.temp_guard.checkee = add_program_constant_stable(tc, cs,
                            STABLE(value.o));
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
                else if (v->guard_concreteness) {
                    MVMDispProgramOp op;
                    op.code = IS_CONCRETE(value.o)
                            ? MVMDispOpcodeGuardTempConc
                            : MVMDispOpcodeGuardTempTypeObject;
                    op.temp_guard.temp = temp;
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
                MVMuint32 i;
                for (i = 0; i < MVM_VECTOR_ELEMS(v->not_literal_guards); i++) {
                    MVMDispProgramOp op;
                    op.code = MVMDispOpcodeGuardTempNotLiteralObj;
                    op.temp_guard.temp = temp;
                    op.temp_guard.checkee = add_program_constant_obj(tc, cs,
                            v->not_literal_guards[i]);
                    MVM_VECTOR_PUSH(cs->ops, op);
                }
            }
            break;
        case MVM_CALLSITE_ARG_STR:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardTempLiteralStr;
                op.temp_guard.temp = temp;
                op.temp_guard.checkee = add_program_constant_str(tc, cs, value.s);
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        case MVM_CALLSITE_ARG_INT:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardTempLiteralInt;
                op.temp_guard.temp = temp;
                op.temp_guard.checkee = add_program_constant_int(tc, cs, value.i64);
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        case MVM_CALLSITE_ARG_NUM:
            if (v->guard_literal) {
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeGuardTempLiteralInt;
                op.temp_guard.temp = temp;
                op.temp_guard.checkee = add_program_constant_num(tc, cs, value.n64);
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            break;
        default:
            MVM_oops(tc, "Unexpected callsite arg type in emit_capture_guards");
    }
}
static void emit_args_ops(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        compile_state *cs, MVMuint32 callsite_idx) {
    /* Obtain the path to the capture we'll be invoking with. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, record->rec.outcome_capture, &p);

    /* Calculate the length of the untouched tail between the incoming capture
     * and the outcome capture. Thi iss defined as the part of it left untouched
     * by any inserts and drops. We start by assuming all of it is untouched. */
    MVMCallsite *initial_callsite = ((MVMCapture *)cs->rec->initial_capture.capture)->body.callsite;
    MVMuint32 untouched_tail_length = initial_callsite->flag_count;
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(p.path); i++) {
        MVMCallsite *cur_callsite = ((MVMCapture *)p.path[i]->capture)->body.callsite;
        switch (p.path[i]->transformation) {
            case MVMDispProgramRecordingInsert: {
                /* Given:
                 *   arg1, arg2, arg3, arg4
                 * If we insert at index 2, then we get:
                 *   arg1, arg2, inserted, arg3, arg4
                 * So the untouched tail is length 2, or more generally,
                 * (capture length - (index + 1)). */
                MVMuint32 locally_untouched = cur_callsite->flag_count - (p.path[i]->index + 1);
                if (locally_untouched < untouched_tail_length)
                    untouched_tail_length = locally_untouched;
                break;
            }
            case MVMDispProgramRecordingDrop: {
                /* Given:
                 *   arg1, arg2, arg3, arg4
                 * If we drop arg2 (index 1), then we get:
                 *  arg1, arg3, arg4
                 * Thus the untouched tail is 2, generally (capture length - index). */
                MVMuint32 locally_untouched = cur_callsite->flag_count - p.path[i]->index;
                if (locally_untouched < untouched_tail_length)
                    untouched_tail_length = locally_untouched;
                break;
            }
            case MVMDispProgramRecordingInitial:
                /* This is the initial capture, so nothing to do. */
                break;
        }
    }

    /* If the untouched tail length is the length of the outcome capture, then
     * we just use the incoming one with a certain number of arguments skipped;
     * this is the nice, no-copying, option that hopefully we regularly get. */
    MVMCallsite *outcome_callsite = ((MVMCapture *)record->rec.outcome_capture)->body.callsite;
    if (outcome_callsite->flag_count == untouched_tail_length) {
        MVMDispProgramOp op;
        op.code = MVMDispOpcodeUseArgsTail;
        op.use_arg_tail.skip_args = initial_callsite->flag_count - untouched_tail_length;
        op.use_arg_tail.callsite_idx = callsite_idx;
        MVM_VECTOR_PUSH(cs->ops, op);
    }

    /* If the untouhced tail is shorter, then we have changes that mean we
     * need to produce a new args buffer. */
    else if (outcome_callsite->flag_count > untouched_tail_length) {
        /* We use an area of the temporaries to model the argument list. We need
         * to produce those not in the tail. However, we need to know the length
         * of the non-arg temporaries before we begin, and some kinds of value
         * might trigger other reads (attribute chains). So we first make a pass
         * through to trigger loads of such values, and decide what we'll need to
         * do with each of the values. */
        MVMuint32 num_to_produce = outcome_callsite->flag_count - untouched_tail_length;
        MVMuint32 i;
        typedef enum {
            SetFromTemporary,
            SetFromInitialCapture
        } Action;
        typedef struct {
            Action action;
            MVMuint32 index;
        } ArgProduction;
        ArgProduction arg_prod[num_to_produce];
        for (i = 0; i < num_to_produce; i++) {
            /* Work out the source of this arg in the capture. For the rationale
             * for this algorithm, see MVM_disp_program_record_track_arg. */
            MVMint32 j;
            MVMuint32 real_index = i;
            MVMint32 found_value_index = -1;
            for (j = MVM_VECTOR_ELEMS(p.path) - 1; j >= 0; j--) {
                switch (p.path[j]->transformation) {
                    case MVMDispProgramRecordingInsert:
                        if (p.path[j]->index == real_index) {
                            found_value_index = p.path[j]->value_index;
                            break;
                        }
                        else {
                            if (real_index > p.path[j]->index)
                                real_index--;
                        }
                        break;
                    case MVMDispProgramRecordingDrop:
                        if (real_index >= p.path[j]->index)
                            real_index++;
                        break;
                    case MVMDispProgramRecordingInitial:
                        break;
                }
            }
            if (found_value_index >= 0) {
                /* It's some kind of value load other than an initial arg. We
                 * can be smarter here in the future if we wish, e.g. for a
                 * constant we can load it directly into the args temporary. */
                arg_prod[i].action = SetFromTemporary;
                arg_prod[i].index = get_temp_holding_value(tc, cs, found_value_index);
            }
            else {
                /* It's a load of an initial argument. */
                arg_prod[i].action = SetFromInitialCapture;
                arg_prod[i].index = real_index;
            }
        }

        /* We now have all temporaries other than the args buffer used. Emit
         * instructions to set that up. */
        MVMuint32 args_base_temp = MVM_VECTOR_ELEMS(cs->value_temps);
        for (i = 0; i < num_to_produce; i++) {
            MVMDispProgramOp op;
            op.code = arg_prod[i].action == SetFromTemporary
                ? MVMDispOpcodeSet
                : MVMDispOpcodeLoadCaptureValue;
            op.load.temp = args_base_temp + i;
            op.load.idx = arg_prod[i].index;
            MVM_VECTOR_PUSH(cs->ops, op);
        }

        /* Finally, the instruction to copy what we can from the args tail. */
        MVMDispProgramOp op;
        op.code = MVMDispOpcodeCopyArgsTail;
        op.copy_arg_tail.tail_args = untouched_tail_length;
        op.copy_arg_tail.callsite_idx = callsite_idx;
        MVM_VECTOR_PUSH(cs->ops, op);
        cs->args_buffer_temps = outcome_callsite->flag_count;
    }

    /* If somehow the untouched tail length is *longer*, then we're deeply
     * confused. */
    else {
        MVM_oops(tc, "Impossible untouched arg tail length calculated in dispatch program");
    }

    /* Cleanup. */
    MVM_VECTOR_DESTROY(p.path);
}
static void process_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    /* Dump the recording if we're debugging. */
    dump_recording(tc, record);

    /* Go through the values and compile the guards associated with them,
     * along with any loads. */
    compile_state cs;
    cs.rec = &(record->rec);
    MVM_VECTOR_INIT(cs.ops, 8);
    MVM_VECTOR_INIT(cs.gc_constants, 4);
    MVM_VECTOR_INIT(cs.constants, 4);
    MVM_VECTOR_INIT(cs.value_temps, 4);
    cs.args_buffer_temps = 0;
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(record->rec.values); i++) {
        MVMDispProgramRecordingValue *v = &(record->rec.values[i]);
        if (v->source == MVMDispProgramRecordingCaptureValue) {
            emit_capture_guards(tc, &cs, v);
        }
        else if (v->source == MVMDispProgramRecordingAttributeValue) {
            emit_attribute_guards(tc, &cs, v, i);
        }
    }

    /* Emit required ops to deliver the dispatch outcome. */
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_VALUE: {
            /* Ensure the value is in a temporary, then emit the op to set the
             * result. */
            MVMuint32 temp = get_temp_holding_value(tc, &cs, record->rec.outcome_value);
            MVMDispProgramOp op;
            switch (record->outcome.result_kind) {
                case MVM_reg_obj:
                    op.code = MVMDispOpcodeResultValueObj;
                    break;
                case MVM_reg_int64:
                    op.code = MVMDispOpcodeResultValueInt;
                    break;
                case MVM_reg_num64:
                    op.code = MVMDispOpcodeResultValueNum;
                    break;
                case MVM_reg_str:
                    op.code = MVMDispOpcodeResultValueStr;
                    break;
                default:
                    MVM_oops(tc, "Unknown result kind in dispatch value outcome");
            }
            op.res_value.temp = temp;
            MVM_VECTOR_PUSH(cs.ops, op);
            break;
        }
        case MVM_DISP_OUTCOME_BYTECODE:
        case MVM_DISP_OUTCOME_CFUNCTION: {
            /* Make sure we load the invokee into a temporary before we go any
             * further. This is the last temporary we add before dealing with
             * args. Also put callsite into constant table. */
            MVMuint32 temp_invokee = get_temp_holding_value(tc, &cs, record->rec.outcome_value);
            MVMuint32 callsite_idx = add_program_constant_callsite(tc, &cs,
                    ((MVMCapture *)record->rec.outcome_capture)->body.callsite);

            /* Produce the args op(s), and then add the dispatch op. */
            emit_args_ops(tc, record, &cs, callsite_idx);
            MVMDispProgramOp op;
            op.code = record->outcome.kind == MVM_DISP_OUTCOME_BYTECODE
                ? MVMDispOpcodeResultBytecode
                : MVMDispOpcodeResultCFunction;
            op.res_code.temp_invokee = temp_invokee;
            MVM_VECTOR_PUSH(cs.ops, op);
            break;
        }
        default:
            MVM_oops(tc, "Unimplemented dispatch outcome compilation");
    }

    /* Create dispatch program description. */
    MVMDispProgram *dp = MVM_malloc(sizeof(MVMDispProgram));
    dp->constants = cs.constants;
    dp->gc_constants = cs.gc_constants;
    dp->num_gc_constants = MVM_VECTOR_ELEMS(cs.gc_constants);
    dp->ops = cs.ops;
    dp->num_ops = MVM_VECTOR_ELEMS(cs.ops);
    dp->num_temporaries = MVM_VECTOR_ELEMS(cs.value_temps) + cs.args_buffer_temps;
    dp->first_args_temporary = MVM_VECTOR_ELEMS(cs.value_temps);

    /* Clean up (we don't free most of the vectors because we've given them
     * over to the MVMDispProgram). */
    MVM_VECTOR_DESTROY(cs.value_temps);

    /* Dump the program if we're debugging. */
    dump_program(tc, dp);

    /* Transition the inline cache to incorporate this dispatch program. */
    MVM_disp_inline_cache_transition(tc, record->ic_entry_ptr, record->ic_entry,
            record->update_sf,
            ((MVMCapture *)record->rec.initial_capture.capture)->body.callsite,
            dp);
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
            MVMFrame *caller = find_calling_frame(record->common.prev);
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
            MVM_disp_program_recording_destroy(tc, &(record->rec));
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = find_calling_frame(tc->stack_top->prev);
            MVM_frame_dispatch(tc, record->outcome.code, record->outcome.args, -1);
            if (thunked)
                *thunked = 1;
            return 0;
        case MVM_DISP_OUTCOME_CFUNCTION:
            process_recording(tc, record);
            MVM_disp_program_recording_destroy(tc, &(record->rec));
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = find_calling_frame(tc->stack_top->prev);
            record->outcome.c_func(tc, record->outcome.args);
            return 1;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}

/* Interpret a dispatch program. */
#define GET_ARG MVMRegister val = args->source[args->map[op->arg_guard.arg_idx]]
MVMint64 MVM_disp_program_run(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallStackDispatchRun *record) {
    MVMArgs *args = &(record->arg_info);
    MVMuint32 i;
    MVMArgs invoke_args;

    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            /* Argument guard ops. */
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

            /* Temporary guard ops. */
            case MVMDispOpcodeGuardTempType: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->temp_guard.checkee])
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempTypeConc: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->temp_guard.checkee]
                        || !IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempTypeTypeObject: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op->temp_guard.checkee]
                        || IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempConc: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (!IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempTypeObject: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (IS_CONCRETE(val.o))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempLiteralObj: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (val.o != (MVMObject *)dp->gc_constants[op->temp_guard.checkee])
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempLiteralStr: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (!MVM_string_equal(tc, val.s, (MVMString *)dp->gc_constants[op->temp_guard.checkee]))
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempLiteralInt: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (val.i64 != dp->constants[op->temp_guard.checkee].i64)
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempLiteralNum: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (val.n64 != dp->constants[op->temp_guard.checkee].n64)
                    goto rejection;
                break;
            }
            case MVMDispOpcodeGuardTempNotLiteralObj: {
                MVMRegister val = record->temps[op->temp_guard.temp];
                if (val.o == (MVMObject *)dp->gc_constants[op->temp_guard.checkee])
                    goto rejection;
                break;
            }

            /* Load ops. */
            case MVMDispOpcodeLoadCaptureValue:
                record->temps[op->load.temp] = args->source[args->map[op->load.idx]];
                break;
            case MVMDispOpcodeLoadConstantObjOrStr:
                record->temps[op->load.temp].o = (MVMObject *)dp->gc_constants[op->load.idx];
                break;
            case MVMDispOpcodeLoadConstantInt:
                record->temps[op->load.temp].i64 = dp->constants[op->load.idx].i64;
                break;
            case MVMDispOpcodeLoadConstantNum:
                record->temps[op->load.temp].n64 = dp->constants[op->load.idx].n64;
                break;
            case MVMDispOpcodeLoadAttributeObj: {
                MVMObject *o = MVM_p6opaque_read_object(tc,
                        record->temps[op->load.temp].o, op->load.idx);
                record->temps[op->load.temp].o = o ? o : tc->instance->VMNull;
                break;
            }
            case MVMDispOpcodeLoadAttributeInt:
                record->temps[op->load.temp].i64 = MVM_p6opaque_read_int64(tc,
                        record->temps[op->load.temp].o, op->load.idx);
                break;
            case MVMDispOpcodeLoadAttributeNum:
                record->temps[op->load.temp].n64 = MVM_p6opaque_read_num64(tc,
                        record->temps[op->load.temp].o, op->load.idx);
                break;
            case MVMDispOpcodeLoadAttributeStr:
                record->temps[op->load.temp].s = MVM_p6opaque_read_str(tc,
                        record->temps[op->load.temp].o, op->load.idx);
                break;
            case MVMDispOpcodeSet:
                record->temps[op->load.temp] = record->temps[op->load.idx];
                break;

            /* Value result ops. */
            case MVMDispOpcodeResultValueObj: {
                MVM_args_set_dispatch_result_obj(tc, tc->cur_frame,
                        record->temps[op->res_value.temp].o);
                MVM_callstack_unwind_dispatch_run(tc);
                break;
            }
            case MVMDispOpcodeResultValueStr: {
                MVM_args_set_dispatch_result_str(tc, tc->cur_frame,
                        record->temps[op->res_value.temp].s);
                MVM_callstack_unwind_dispatch_run(tc);
                break;
            }
            case MVMDispOpcodeResultValueInt: {
                MVM_args_set_dispatch_result_int(tc, tc->cur_frame,
                        record->temps[op->res_value.temp].i64);
                MVM_callstack_unwind_dispatch_run(tc);
                break;
            }
            case MVMDispOpcodeResultValueNum: {
                MVM_args_set_dispatch_result_num(tc, tc->cur_frame,
                        record->temps[op->res_value.temp].n64);
                MVM_callstack_unwind_dispatch_run(tc);
                break;
            }

            /* Args preparation for invocation result. */
            case MVMDispOpcodeUseArgsTail:
                invoke_args.callsite = dp->constants[op->use_arg_tail.callsite_idx].cs;
                invoke_args.source = args->source;
                invoke_args.map = args->map + op->use_arg_tail.skip_args;
                break;
            case MVMDispOpcodeCopyArgsTail: {
                invoke_args.callsite = dp->constants[op->copy_arg_tail.callsite_idx].cs;
                invoke_args.source = record->temps + dp->first_args_temporary;
                invoke_args.map = MVM_args_identity_map(tc, invoke_args.callsite);
                MVMuint32 to_copy = op->copy_arg_tail.tail_args;
                if (to_copy > 0) {
                    MVMuint32 source_idx = args->callsite->flag_count - to_copy;
                    MVMuint32 target_idx = dp->first_args_temporary +
                            (invoke_args.callsite->flag_count - to_copy);
                    MVMuint32 i;
                    for (i = 0; i < to_copy; i++)
                        record->temps[target_idx++] = args->source[args->map[source_idx++]];
                }
                /* We need to stash this for correct marking of temporaries. */
                record->temp_mark_callsite = invoke_args.callsite;
                break;
            }

            /* Invocation results. */
            case MVMDispOpcodeResultBytecode:
                record->chosen_dp = dp;
                MVM_frame_dispatch(tc, (MVMCode *)record->temps[op->res_code.temp_invokee].o,
                        invoke_args, -1);
                break;
            case MVMDispOpcodeResultCFunction: {
                record->chosen_dp = dp;
                MVMCFunction *wrapper = (MVMCFunction *)record->temps[op->res_code.temp_invokee].o;
                wrapper->body.func(tc, invoke_args);
                MVM_callstack_unwind_dispatch_run(tc);
                break;
            }

            default:
                MVM_oops(tc, "Unknown dispatch program op %d", op->code);
        }
    }

    return 1;
rejection:
    return 0;
}

/* GC mark a dispatch program's GC constants. */
void MVM_disp_program_mark(MVMThreadContext *tc, MVMDispProgram *dp, MVMGCWorklist *worklist) {
    MVMuint32 i;
    for (i = 0; i < dp->num_gc_constants; i++)
        MVM_gc_worklist_add(tc, worklist, &(dp->gc_constants[i]));
}

/* Mark the recording state of a dispatch program. */
static void mark_recording_capture(MVMThreadContext *tc, MVMDispProgramRecordingCapture *cap,
        MVMGCWorklist *worklist) {
    MVM_gc_worklist_add(tc, worklist, &(cap->capture));
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(cap->captures); i++)
        mark_recording_capture(tc, &(cap->captures[i]), worklist);
}
void MVM_disp_program_mark_recording(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMGCWorklist *worklist) {
    MVMuint32 i, j;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *value = &(rec->values[i]);
        switch (value->source) {
            case MVMDispProgramRecordingCaptureValue:
            case MVMDispProgramRecordingAttributeValue:
                /* Nothing to mark. */
                break;
            case MVMDispProgramRecordingLiteralValue:
                if (value->literal.kind == MVM_CALLSITE_ARG_OBJ ||
                        value->literal.kind == MVM_CALLSITE_ARG_STR)
                    MVM_gc_worklist_add(tc, worklist, &(value->literal.value.o));
                break;
            default:
                MVM_panic(1, "Unknown dispatch program value kind to GC mark");
                break;
        }
        MVM_gc_worklist_add(tc, worklist, &(value->tracked));
        for (j = 0; j < MVM_VECTOR_ELEMS(value->not_literal_guards); j++)
            MVM_gc_worklist_add(tc, worklist, &(value->not_literal_guards[i]));
    }
    mark_recording_capture(tc, &(rec->initial_capture), worklist);
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->resume_inits); i++) {
        MVM_gc_worklist_add(tc, worklist, &(rec->resume_inits[i].capture));
    }
    MVM_gc_worklist_add(tc, worklist, &(rec->outcome_capture));
}

/* Mark the temporaries of a dispatch program. */
void MVM_disp_program_mark_run_temps(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallsite *cs, MVMRegister *temps, MVMGCWorklist *worklist) {
    MVMuint32 i;
    for (i = 0; i < cs->flag_count; i++) {
        if (cs->arg_flags[i] & (MVM_CALLSITE_ARG_OBJ | MVM_CALLSITE_ARG_STR)) {
            MVMuint32 temp_idx = dp->first_args_temporary + i;
            MVM_gc_worklist_add(tc, worklist, &(temps[temp_idx]));
        }
    }
}

/* Mark the outcome of a dispatch program. */
void MVM_disp_program_mark_outcome(MVMThreadContext *tc, MVMDispProgramOutcome *outcome,
        MVMGCWorklist *worklist) {
    switch (outcome->kind) {
        case MVM_DISP_OUTCOME_FAILED:
        case MVM_DISP_OUTCOME_CFUNCTION:
            /* Nothing to mark for these. */
            break;
        case MVM_DISP_OUTCOME_EXPECT_DELEGATE:
            MVM_gc_worklist_add(tc, worklist, &(outcome->delegate_capture));
            break;
        case MVM_DISP_OUTCOME_VALUE:
            if (outcome->result_kind == MVM_reg_obj || outcome->result_kind == MVM_reg_str)
                MVM_gc_worklist_add(tc, worklist, &(outcome->result_value.o));
            break;
        case MVM_DISP_OUTCOME_BYTECODE:
            MVM_gc_worklist_add(tc, worklist, &(outcome->code));
            break;
    }
}

/* Release memory associated with a dispatch program. */
void MVM_disp_program_destroy(MVMThreadContext *tc, MVMDispProgram *dp) {
    MVM_free(dp->constants);
    MVM_free(dp->gc_constants);
    MVM_free(dp->ops);
    MVM_free(dp);
}

/* Free the memory associated with a dispatch program recording. */
void destroy_recording_capture(MVMThreadContext *tc, MVMDispProgramRecordingCapture *cap) {
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(cap->captures); i++)
        destroy_recording_capture(tc, &(cap->captures[i]));
    MVM_VECTOR_DESTROY(cap->captures);
}
void MVM_disp_program_recording_destroy(MVMThreadContext *tc, MVMDispProgramRecording *rec) {
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++)
        MVM_VECTOR_DESTROY(rec->values[i].not_literal_guards);
    MVM_VECTOR_DESTROY(rec->values);
    MVM_VECTOR_DESTROY(rec->resume_inits);
    destroy_recording_capture(tc, &(rec->initial_capture));
}
