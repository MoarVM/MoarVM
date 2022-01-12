#include "moar.h"

/* Debug dumping, to figure out what we're recording and what programs we are
 * inferring from those recordings. */
#ifndef MVM_DISP_DUMP_RECORDINGS
#define MVM_DISP_DUMP_RECORDINGS     0
#endif
#ifndef MVM_DISP_DUMP_PROGRAMS
#define MVM_DISP_DUMP_PROGRAMS       0
#endif

#if MVM_DISP_DUMP_RECORDINGS
static void dump_recording_capture(MVMThreadContext *tc,
        MVMDispProgramRecordingCapture *capture, MVMuint32 indent,
        MVMDispProgramRecording *rec) {
    char *indent_str = alloca(indent + 1);
    memset(indent_str, ' ', indent);
    indent_str[indent] = '\0';
    switch (capture->transformation) {
        case MVMDispProgramRecordingInitial:
            fprintf(stderr, "%sInitial (%d args, %d pos)\n", indent_str,
                    ((MVMCapture *)capture->capture)->body.callsite->flag_count,
                    ((MVMCapture *)capture->capture)->body.callsite->num_pos);
            break;
        case MVMDispProgramRecordingResumeInitial:
            fprintf(stderr, "%sInitial Resume State (%d args, %d pos)\n", indent_str,
                    ((MVMCapture *)capture->capture)->body.callsite->flag_count,
                    ((MVMCapture *)capture->capture)->body.callsite->num_pos);
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
            case MVMDispProgramRecordingResumeInitCaptureValue:
                fprintf(stderr, "    %d Resume initialization argument %d from resumption %d\n", i,
                        v->resume_capture.index, v->resume_capture.resumption_level);
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
                    case MVM_CALLSITE_ARG_UINT:
                        fprintf(stderr, "    %d Literal uint value %"PRIu64"\n", i,
                                v->literal.value.u64);
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
                fprintf(stderr, "    %d Attribute value from offset %d of value %d\n", i,
                        v->attribute.offset, v->attribute.from_value);
                break;
            case MVMDispProgramRecordingHOWValue:
                fprintf(stderr, "    %d HOW of value %d\n", i, v->how.from_value);
                break;
            case MVMDispProgramRecordingLookupValue:
                fprintf(stderr, "    %d Lookup in hash value %d using key value %d\n", i,
                        v->lookup.lookup_index, v->lookup.key_index);
                break;
            case MVMDispProgramRecordingResumeStateValue:
                fprintf(stderr, "    %d Resume state (resumption %d)\n", i, v->resumption.index);
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
            else {
                if (v->guard_concreteness)
                    fprintf(stderr, "      Guard concreteness\n");
                if (v->guard_hll)
                    fprintf(stderr, "      Guard HLL\n");
            }
        }
        if (rec->resume_kind != MVMDispProgramRecordingResumeNone)
            for (MVMuint32 j = 0; j < MVM_VECTOR_ELEMS(rec->resumptions); j++)
                if ((MVMint32)i == rec->resumptions[j].new_resume_state_value)
                    fprintf(stderr, "      Used as new resume state for resumption %d\n", j);
    }
};
static void dump_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    MVMuint32 is_resume = record->rec.resume_kind != MVMDispProgramRecordingResumeNone;

    fprintf(stderr, "Dispatch recording%s\n", is_resume ? " (resume)" : "");

    char *backtrace_line = MVM_exception_backtrace_line(tc, tc->cur_frame, 0,
		    *(tc->interp_cur_op));
    fprintf(stderr, "%s\n", backtrace_line);
    MVM_free(backtrace_line);

    fprintf(stderr, "  Captures:\n");
    dump_recording_capture(tc, &(record->rec.initial_capture), 4, &(record->rec));
    if (is_resume)
        for (MVMuint32 i = 0; i < MVM_VECTOR_ELEMS(record->rec.resumptions); i++)
            dump_recording_capture(tc, &(record->rec.resumptions[i].initial_resume_capture), 4, &(record->rec));
    fprintf(stderr, "  Values:\n");
    dump_recording_values(tc, &(record->rec));
    fprintf(stderr, "  Outcome:\n");
    switch (record->rec.map_bind_outcome_to_resumption) {
        case MVMDispProgramRecordingBindControlFailure:
            fprintf(stderr, "    Bind failure mapped to resumption with flag %"PRIu32"\n",
                record->rec.bind_failure_resumption_flag);
            break;
        case MVMDispProgramRecordingBindControlAll:
            fprintf(stderr, "    Bind outcome mapped to resumption; failure=%"PRIu32", success=%"PRIu32"\n",
                record->rec.bind_failure_resumption_flag,
                record->rec.bind_success_resumption_flag);
            break;
    }
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_VALUE:
            fprintf(stderr, "    Value %d\n", record->rec.outcome_value);
            break;
        case MVM_DISP_OUTCOME_BYTECODE:
            fprintf(stderr, "    Run bytecode of value %d\n", record->rec.outcome_value);
            break;
        case MVM_DISP_OUTCOME_CFUNCTION:
            fprintf(stderr, "    Run C function of value %d\n", record->rec.outcome_value);
            break;
        case MVM_DISP_OUTCOME_FOREIGNCODE:
            fprintf(stderr, "    Run foreign function of value %d\n", record->rec.outcome_value);
            break;
        default:
            printf("    Unknown\n");
    }
}
#else
#define dump_recording(tc, r) do {} while (0)
#endif /* MVM_DISP_DUMP_RECORDINGS */

#if MVM_DISP_DUMP_PROGRAMS
static void dump_program(MVMThreadContext *tc, MVMDispProgram *dp) {
    if (dp->first_args_temporary == dp->num_temporaries)
        fprintf(stderr, "Dispatch program %p (%d temporaries)\n", dp, dp->num_temporaries);
    else
        fprintf(stderr, "Dispatch program %p (%d temporaries, args from %d)\n", dp,
                dp->num_temporaries, dp->first_args_temporary);

    char *backtrace_line = MVM_exception_backtrace_line(tc, tc->cur_frame, 0,
		    *(tc->interp_cur_op));
    fprintf(stderr, "%s\n", backtrace_line);
    MVM_free(backtrace_line);

    MVMuint32 i;
    fprintf(stderr, "  Ops:\n");
    for (i = 0; i < dp->num_ops; i++) {
        MVMDispProgramOp *op = &(dp->ops[i]);
        switch (op->code) {
            /* Resumption related opcodes. */
            case MVMDispOpcodeStartResumption:
                fprintf(stderr, "    Start a resumption dispatch\n");
                break;
            case MVMDispOpcodeNextResumption:
                fprintf(stderr, "    Move to next resumption\n");
                break;
            case MVMDispOpcodeResumeTopmost: {
                char *c_str = MVM_string_utf8_encode_C_string(tc,
                       op->resume.disp->id);
                fprintf(stderr, "    Resume topmost dispatch if it is %s\n", c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeResumeCaller: {
                char *c_str = MVM_string_utf8_encode_C_string(tc,
                       op->resume.disp->id);
                fprintf(stderr, "    Resume caller dispatch if it is %s\n", c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeGuardResumeInitCallsite:
                fprintf(stderr, "    Check resume init state callsite is %d\n",
                        op->resume_init_callsite.callsite_idx);
                break;
            case MVMDispOpcodeGuardNoResumptionTopmost:
                fprintf(stderr, "    Guard there is no further resumption in topmost dispatch\n");
                break;
            case MVMDispOpcodeGuardNoResumptionCaller:
                fprintf(stderr, "    Guard there is no further resumption in caller dispatch\n");
                break;
            case MVMDispOpcodeUpdateResumeState:
                fprintf(stderr, "    Update resume state to temp %d\n",
                        op->res_value.temp);
                break;
            /* Opcodes that guard on values in argument slots */
            case MVMDispOpcodeGuardArgType:
                fprintf(stderr, "    Guard arg %d (type=%s)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardArgTypeConc:
                fprintf(stderr, "    Guard arg %d (type=%s, concrete)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
           case MVMDispOpcodeGuardArgTypeTypeObject:
                fprintf(stderr, "    Guard arg %d (type=%s, type object)\n",
                        op->arg_guard.arg_idx,
                        ((MVMSTable *)dp->gc_constants[op->arg_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardArgConc:
                fprintf(stderr, "    Guard arg %d (concrete)\n",
                        op->arg_guard.arg_idx);
                break;
            case MVMDispOpcodeGuardArgTypeObject:
                fprintf(stderr, "    Guard arg %d (type object)\n",
                        op->arg_guard.arg_idx);
                break;
            case MVMDispOpcodeGuardArgLiteralObj:
                fprintf(stderr, "    Guard arg %d (literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardArgLiteralStr: {
                char *c_str = MVM_string_utf8_encode_C_string(tc, 
                        ((MVMString *)dp->gc_constants[op->arg_guard.checkee]));
                fprintf(stderr, "    Guard arg %d (literal string '%s')\n",
                        op->arg_guard.arg_idx, c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeGuardArgLiteralInt:
                fprintf(stderr, "    Guard arg %d (literal integer %"PRIi64")\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].i64);
                break;
            case MVMDispOpcodeGuardArgLiteralNum:
                fprintf(stderr, "    Guard arg %d (literal number %g)\n",
                        op->arg_guard.arg_idx,
                        dp->constants[op->arg_guard.checkee].n64);
                break;
            case MVMDispOpcodeGuardArgNotLiteralObj:
                fprintf(stderr, "    Guard arg %d (not literal object of type %s)\n",
                        op->arg_guard.arg_idx,
                        STABLE(((MVMObject *)dp->gc_constants[op->arg_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardArgHLL: {
                MVMHLLConfig *hll = dp->constants[op->arg_guard.checkee].hll;
                char *hll_name = hll ? MVM_string_utf8_encode_C_string(tc, hll->name) : NULL;
                fprintf(stderr, "    Guard arg %d (hll=%s)\n",
                        op->arg_guard.arg_idx, hll_name ? hll_name : "<none>");
                MVM_free(hll_name);
                break;
            }

            /* Opcodes that guard on values in temporaries */
            case MVMDispOpcodeGuardTempType:
                fprintf(stderr, "    Guard temp %d (type=%s)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardTempTypeConc:
                fprintf(stderr, "    Guard temp %d (type=%s, concrete)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
           case MVMDispOpcodeGuardTempTypeTypeObject:
                fprintf(stderr, "    Guard temp %d (type=%s, type object)\n",
                        op->temp_guard.temp,
                        ((MVMSTable *)dp->gc_constants[op->temp_guard.checkee])->debug_name);
                break;
            case MVMDispOpcodeGuardTempConc:
                fprintf(stderr, "    Guard temp %d (concrete)\n",
                        op->temp_guard.temp);
                break;
            case MVMDispOpcodeGuardTempTypeObject:
                fprintf(stderr, "    Guard temp %d (type object)\n",
                        op->temp_guard.temp);
                break;
            case MVMDispOpcodeGuardTempLiteralObj:
                fprintf(stderr, "    Guard temp %d (literal object of type %s)\n",
                        op->temp_guard.temp,
                        STABLE(((MVMObject *)dp->gc_constants[op->temp_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardTempLiteralStr: {
                char *c_str = MVM_string_utf8_encode_C_string(tc, 
                        ((MVMString *)dp->gc_constants[op->temp_guard.checkee]));
                fprintf(stderr, "    Guard temp %d (literal string '%s')\n",
                        op->temp_guard.temp, c_str);
                MVM_free(c_str);
                break;
            }
            case MVMDispOpcodeGuardTempLiteralInt:
                fprintf(stderr, "    Guard temp %d (literal integer %"PRIi64")\n",
                        op->temp_guard.temp,
                        dp->constants[op->temp_guard.checkee].i64);
                break;
            case MVMDispOpcodeGuardTempLiteralNum:
                fprintf(stderr, "    Guard temp %d (literal number %g)\n",
                        op->temp_guard.temp,
                        dp->constants[op->temp_guard.checkee].n64);
                break;
            case MVMDispOpcodeGuardTempNotLiteralObj:
                fprintf(stderr, "    Guard temp %d (not literal object of type %s)\n",
                        op->temp_guard.temp,
                        STABLE(((MVMObject *)dp->gc_constants[op->temp_guard.checkee]))->debug_name);
                break;
            case MVMDispOpcodeGuardTempHLL: {
                MVMHLLConfig *hll = dp->constants[op->temp_guard.checkee].hll;
                char *hll_name = hll ? MVM_string_utf8_encode_C_string(tc, hll->name) : NULL;
                fprintf(stderr, "    Guard temp %d (hll=%s)\n",
                        op->temp_guard.temp, hll_name ? hll_name : "<none>");
                MVM_free(hll_name);
                break;
            }

            /* Opcodes that load values into temporaries. */
            case MVMDispOpcodeLoadCaptureValue:
                fprintf(stderr, "    Load argument %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadResumeInitValue:
                fprintf(stderr, "    Load resume init state argument %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadResumeState:
                fprintf(stderr, "    Load resume state into temporary %d\n", op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantObjOrStr:
                fprintf(stderr, "    Load collectable constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantInt:
                fprintf(stderr, "    Load integer constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadConstantNum:
                fprintf(stderr, "    Load number constant at index %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeObj:
                fprintf(stderr, "    Deference object attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeInt:
                fprintf(stderr, "    Deference integer attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeNum:
                fprintf(stderr, "    Deference number attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadAttributeStr:
                fprintf(stderr, "    Deference string attribute at offset %d in temporary %d\n",
                        op->load.idx, op->load.temp);
                break;
            case MVMDispOpcodeLoadHOW:
                fprintf(stderr, "    Put into temporary %d HOW of temporary %d\n",
                        op->load.temp, op->load.idx);
                break;
            case MVMDispOpcodeLookup:
                fprintf(stderr, "    Hash table lookup into temporary %d with key from %d\n",
                        op->load.temp, op->load.idx);
                break;
            case MVMDispOpcodeSet:
                fprintf(stderr, "    Copy temporary %d into temporary %d\n",
                        op->load.idx, op->load.temp);
                break;

            /* Opcodes that set a result value */
            case MVMDispOpcodeResultValueObj:
                fprintf(stderr, "    Set result object value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueStr:
                fprintf(stderr, "    Set result string value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueInt:
                fprintf(stderr, "    Set result integer value from temporary %d\n",
                        op->res_value.temp);
                break;
            case MVMDispOpcodeResultValueNum:
                fprintf(stderr, "    Set result num value from temporary %d\n",
                        op->res_value.temp);
                break;

            /* Opcodes that handle invocation results. */
            case MVMDispOpcodeBindFailureToResumption:
                fprintf(stderr, "    Map bind failure to resumption with flag %"PRIi32"\n",
                        op->bind_control_resumption.failure_flag);
                break;
            case MVMDispOpcodeUseArgsTail:
                fprintf(stderr, "    Skip first %d args of incoming capture; callsite from %d\n",
                        op->use_arg_tail.skip_args, op->use_arg_tail.callsite_idx);
                break;
            case MVMDispOpcodeCopyArgsTail:
                fprintf(stderr, "    Copy final %d args of incoming capture; callsite from %d\n",
                        op->copy_arg_tail.tail_args, op->copy_arg_tail.callsite_idx);
                break;
            case MVMDispOpcodeResultBytecode:
                fprintf(stderr, "    Invoke MVMCode in temporary %d\n",
                        op->res_code.temp_invokee);
                break;
            case MVMDispOpcodeResultCFunction:
                fprintf(stderr, "    Invoke MVMCFunction in temporary %d\n",
                        op->res_code.temp_invokee);
                break;
            case MVMDispOpcodeResultForeignCode:
                fprintf(stderr, "    Invoke foreign function in temporary %d\n",
                        op->res_code.temp_invokee);
                break;

            default:
                fprintf(stderr, "    UNKNOWN OP %d\n", op->code);
        }
    }

    if (dp->num_resumptions) {
        fprintf(stderr, "  Resumptions:\n");
        for (i = 0; i < dp->num_resumptions; i++) {
            MVMDispProgramResumption *res = &(dp->resumptions[i]);
            char *c_id = MVM_string_utf8_encode_C_string(tc, res->disp->id);
            fprintf(stderr, "    Dispatcher %s\n", c_id);
            MVM_free(c_id);
            if (res->init_values) {
                fprintf(stderr, "      Initialization arguments:\n");
                MVMuint32 j;
                for (j = 0; j < res->init_callsite->flag_count; j++) {
                    MVMDispProgramResumptionInitValue iv = res->init_values[j];
                    switch (iv.source) {
                        case MVM_DISP_RESUME_INIT_ARG:
                            fprintf(stderr, "        Initial argument %d\n", iv.index);
                            break;
                        case MVM_DISP_RESUME_INIT_CONSTANT_OBJ:
                            fprintf(stderr, "        Object constant at %d\n", iv.index);
                            break;
                        case MVM_DISP_RESUME_INIT_CONSTANT_INT:
                            fprintf(stderr, "        Integer constant at %d\n", iv.index);
                            break;
                        case MVM_DISP_RESUME_INIT_CONSTANT_NUM:
                            fprintf(stderr, "        Number constant at %d\n", iv.index);
                            break;
                        case MVM_DISP_RESUME_INIT_TEMP:
                            fprintf(stderr, "        Temporary %d\n", iv.index);
                            break;
                        default:
                        fprintf(stderr, "        UNKNOWN\n");
                    }
                }
            }
            else {
                fprintf(stderr, "      Initialization arguments match dispatcher arguments\n");
            }
        }
    }
}
#else
#define dump_program(tc, dp) do {} while (0)
#endif /* MVM_DISP_DUMP_PROGRAMS */

/* Run a dispatch callback, which will record a dispatch program. */
static MVMFrame * find_calling_frame(MVMThreadContext *tc, MVMCallStackRecord *prev) {
    /* Typically, we'll have the frame right off, but if there was flattening
     * or bind failure, we'll need to skip some. */
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_init(tc, &iter, prev);
    if (!MVM_callstack_iter_move_next(tc, &iter))
        MVM_oops(tc, "Cannot find calling frame during dispatch resumption recording");
    return MVM_callstack_iter_current_frame(tc, &iter);
}
static MVMuint32 calculate_inline_cache_size(MVMThreadContext *tc, MVMDispInlineCacheEntry *ice) {
    switch (MVM_disp_inline_cache_get_kind(tc, ice)) {
        case MVM_INLINE_CACHE_KIND_INITIAL:
        case MVM_INLINE_CACHE_KIND_INITIAL_FLATTENING:
            return 0;
        case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH:
        case MVM_INLINE_CACHE_KIND_MONOMORPHIC_DISPATCH_FLATTENING:
            return 1;
        case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH:
            return ((MVMDispInlineCacheEntryPolymorphicDispatch *)ice)->num_dps;
        case MVM_INLINE_CACHE_KIND_POLYMORPHIC_DISPATCH_FLATTENING:
            return ((MVMDispInlineCacheEntryPolymorphicDispatchFlattening *)ice)->num_dps;
        default:
            MVM_exception_throw_adhoc(tc, "Unrecognized inline cache entry");
    }
}
static void run_dispatch(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMDispDefinition *disp, MVMObject *capture) {
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
        record->outcome.delegate_disp = NULL;
        record->outcome.delegate_capture = NULL;
        ((MVMCFunction *)dispatch)->body.func(tc, dispatch_args);
        MVM_callstack_unwind_dispatch_record(tc);
    }
    else if (REPR(dispatch)->ID == MVM_REPR_ID_MVMCode) {
        record->outcome.kind = MVM_DISP_OUTCOME_EXPECT_DELEGATE;
        record->outcome.delegate_disp = NULL;
        record->outcome.delegate_capture = NULL;
        tc->cur_frame = find_calling_frame(tc, tc->stack_top->prev);
        MVM_frame_dispatch(tc, (MVMCode *)dispatch, dispatch_args, -1);
    }
    else {
        MVM_panic(1, "dispatch callback only supported as a MVMCFunction or MVMCode");
    }
}
void MVM_disp_program_run_dispatch(MVMThreadContext *tc, MVMDispDefinition *disp,
        MVMArgs arg_info, MVMDispInlineCacheEntry **ic_entry_ptr,
        MVMDispInlineCacheEntry *ic_entry, MVMStaticFrame *update_sf) {
#if MVM_GC_DEBUG
    {
        MVMuint32 i;
        MVMuint32 found = 0;
        for (i = 0; i < update_sf->body.inline_cache.num_entries; i++) {
            if (ic_entry_ptr == update_sf->body.inline_cache.entries + i) {
                found = 1;
                break;
            }
        }
        if (!found)
            MVM_panic(1, "Incorrect static frame root used for barriering inline cache");
    }
#endif

    /* Calculate the size of the current inline cache (must be done before we
     * hit a safepoint, otherwise we may use the current inline cache content
     * after it is freed). */
    MVMuint32 inline_cache_size = calculate_inline_cache_size(tc, ic_entry);

    /* Form an argument capture. */
    MVMObject *capture;
    MVMROOT(tc, update_sf, {
        capture = MVM_capture_from_args(tc, arg_info);
    });

    /* Push a dispatch recording frame onto the callstack; this is how we'll
     * keep track of the current recording state. */
    MVMCallStackDispatchRecord *record = MVM_callstack_allocate_dispatch_record(tc);
    record->arg_info = arg_info;
    record->current_capture.o = NULL;
    record->rec.initial_capture.capture = NULL; /* In case we mark during setup */
    record->rec.initial_capture.transformation = MVMDispProgramRecordingInitial;
    record->rec.resume_kind = MVMDispProgramRecordingResumeNone;
    MVM_VECTOR_INIT(record->rec.initial_capture.captures, 8);
    MVM_VECTOR_INIT(record->rec.values, 16);
    MVM_VECTOR_INIT(record->rec.resume_inits, 4);
    record->rec.outcome_capture = NULL;
    record->rec.map_bind_outcome_to_resumption = MVMDispProgramRecordingBindControlNone;
    record->rec.initial_capture.capture = capture;
    record->rec.inline_cache_size = inline_cache_size;
    record->rec.do_not_install = 0;
    record->initial_disp = disp;
    record->ic_entry_ptr = ic_entry_ptr;
    record->ic_entry = ic_entry;
    record->update_sf = update_sf;
    record->produced_dp = NULL;

    /* The dispatchers should not return anything; stash away the original
     * return type so we can reinstate it when we are done recording. */
    record->orig_return_type = tc->cur_frame->return_type;
    tc->cur_frame->return_type = MVM_RETURN_VOID;

    /* Run the dispatcher. */
    run_dispatch(tc, record, disp, capture);
}

/* Run a resume callback. */
static void run_resume(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMDispDefinition *disp, MVMObject *capture) {
    MVMCallsite *disp_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ);
    record->current_disp = disp;
    record->current_capture.o = capture;
    MVMArgs resume_args = {
        .callsite = disp_callsite,
        .source = &(record->current_capture),
        .map = MVM_args_identity_map(tc, disp_callsite)
    };
    MVMObject *resume = disp->resume;
    if (REPR(resume)->ID == MVM_REPR_ID_MVMCode) {
        record->outcome.kind = MVM_DISP_OUTCOME_EXPECT_DELEGATE;
        record->outcome.delegate_disp = NULL;
        record->outcome.delegate_capture = NULL;
        tc->cur_frame = find_calling_frame(tc, tc->stack_top->prev);
        MVM_frame_dispatch(tc, (MVMCode *)resume, resume_args, -1);
    }
    else {
        MVM_panic(1, "resume callback only supported as a MVMCode");
    }
}

/* Gets the size of the inline cache at the site we're currently recording a
 * dispatch program for. (Useful for when dispatchers want to do something
 * different when there's megamorphic callsites.) */
MVMint64 MVM_disp_program_record_get_inline_cache_size(MVMThreadContext *tc) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    return record->rec.inline_cache_size;
}

/* Indicates that this dispatch program should not be installed at the callsite
 * but rather discarded on unwind. */
void MVM_disp_program_record_do_not_install(MVMThreadContext *tc) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    record->rec.do_not_install = 1;
}

/* Gets the HLL of the static frame where the dispatch program appears. This
 * is useful for getting a reliable HLL even if the dispatch op is inlined. */
MVMHLLConfig * MVM_disp_program_record_get_hll(MVMThreadContext *tc) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    return record->update_sf->body.cu->body.hll_config;
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
    (void)MVM_VECTOR_POP(p->path);
    return 0;
}
static void calculate_capture_path(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMObject *capture, CapturePath *p) {
    if (!find_capture(tc, &(record->rec.initial_capture), capture, p)) {
        /* Not reachable from the initial capture, but what about the currently
         * active resume init capture? */
        if (record->rec.resume_kind != MVMDispProgramRecordingResumeNone) {
            MVMuint32 cur = MVM_VECTOR_ELEMS(record->rec.resumptions) - 1;
            if (find_capture(tc, &(record->rec.resumptions[cur].initial_resume_capture), capture, p))
                return;
        }
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
                case MVM_CALLSITE_ARG_UINT:
                    if (v->literal.value.u64 == value.u64)
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

/* Ensures we have a values used entry for the specified resume init capture
 * argument index. */
static MVMuint32 value_index_resume_capture(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 index) {
    /* Look for an existing such value. */
    MVMuint32 level = MVM_VECTOR_ELEMS(rec->resumptions) - 1;
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingResumeInitCaptureValue &&
                v->resume_capture.index == index && v->resume_capture.resumption_level == level)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingResumeInitCaptureValue;
    new_value.resume_capture.index = index;
    new_value.resume_capture.resumption_level = level;
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

/* Ensures we have a values used entry for the specified HOW read. */
static MVMuint32 value_index_how(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 from_value) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingHOWValue &&
                v->how.from_value == from_value)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingHOWValue;
    new_value.how.from_value = from_value;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the specified unboxed int. */
static MVMuint32 value_index_unbox_int(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 from_value) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingUnboxValue &&
                v->unbox.from_value == from_value &&
                v->unbox.kind == MVM_CALLSITE_ARG_INT)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingUnboxValue;
    new_value.unbox.from_value = from_value;
    new_value.unbox.kind = MVM_CALLSITE_ARG_INT;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the specified unboxed num. */
static MVMuint32 value_index_unbox_num(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 from_value) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingUnboxValue &&
                v->unbox.from_value == from_value &&
                v->unbox.kind == MVM_CALLSITE_ARG_NUM)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingUnboxValue;
    new_value.unbox.from_value = from_value;
    new_value.unbox.kind = MVM_CALLSITE_ARG_NUM;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the specified unboxed str. */
static MVMuint32 value_index_unbox_str(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 from_value) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingUnboxValue &&
                v->unbox.from_value == from_value &&
                v->unbox.kind == MVM_CALLSITE_ARG_STR)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingUnboxValue;
    new_value.unbox.from_value = from_value;
    new_value.unbox.kind = MVM_CALLSITE_ARG_STR;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the specified lookup table read. */
static MVMuint32 value_index_lookup(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMuint32 lookup_index, MVMuint32 key_index) {
    /* Look for an existing such value. */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingLookupValue &&
                v->lookup.lookup_index == lookup_index &&
                v->lookup.key_index == key_index)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingLookupValue;
    new_value.lookup.lookup_index = lookup_index;
    new_value.lookup.key_index = key_index;
    MVM_VECTOR_PUSH(rec->values, new_value);
    return MVM_VECTOR_ELEMS(rec->values) - 1;
}

/* Ensures we have a values used entry for the resume state. */
static MVMuint32 value_index_resume_state(MVMThreadContext *tc, MVMDispProgramRecording *rec) {
    /* Look for an existing such value. */
    MVMuint32 i;
    MVMuint32 resumption_index = MVM_VECTOR_ELEMS(rec->resumptions) - 1;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *v = &(rec->values[i]);
        if (v->source == MVMDispProgramRecordingResumeStateValue &&
                v->resumption.index == resumption_index)
            return i;
    }

    /* Otherwise, we need to create the value entry. */
    MVMDispProgramRecordingValue new_value;
    memset(&new_value, 0, sizeof(MVMDispProgramRecordingValue));
    new_value.source = MVMDispProgramRecordingResumeStateValue;
    new_value.resumption.index = resumption_index;
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
    MVM_capture_arg(tc, capture, index, &value, &kind);

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
    MVMuint32 is_resume_init_capture = 0;
    for (i = MVM_VECTOR_ELEMS(p.path) - 1; i >= 0 && found_value_index < 0; i--) {
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
            case MVMDispProgramRecordingResumeInitial:
                /* We have reached the initial resume arguments capture, and so
                 * the index is for it. */
                is_resume_init_capture = 1;
                break;
        }
    }
    MVM_VECTOR_DESTROY(p.path);

    /* If we didn't find a value index, then we're referencing the original
     * capture; ensure there's a value index for that. */
    if (found_value_index < 0)
        found_value_index = is_resume_init_capture
            ? value_index_resume_capture(tc, &(record->rec), real_index)
            : value_index_capture(tc, &(record->rec), real_index);

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
        case MVM_CALLSITE_ARG_UINT:
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

MVMObject * MVM_disp_program_record_track_unbox_int(MVMThreadContext *tc, MVMObject *tracked_in) {
    /* Ensure the tracked value is an object type. */
    if (((MVMTracked *)tracked_in)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_oops(tc, "Can only use dispatcher-track-unbox-int on a tracked object");

    /* Resolve the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_in);

    /* Obtain the object and ensure it is concrete; also track its
     * type and concreteness since unboxing safety depends on this. */
    MVMObject *read_from = ((MVMTracked *)tracked_in)->body.value.o;
    if (!IS_CONCRETE(read_from))
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-unbox-int on a concrete object");
    record->rec.values[value_index].guard_type = 1;
    record->rec.values[value_index].guard_concreteness = 1;

    /* Read the value. */
    MVMRegister attr_value;
    attr_value.i64 = MVM_repr_get_int(tc, read_from);

    /* Ensure that we have this value read in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_unbox_int(tc, &(record->rec), value_index);
    if (!record->rec.values[result_value_index].tracked)
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
                attr_value, MVM_CALLSITE_ARG_INT);
    return record->rec.values[result_value_index].tracked;
}

MVMObject * MVM_disp_program_record_track_unbox_num(MVMThreadContext *tc, MVMObject *tracked_in) {
    /* Ensure the tracked value is an object type. */
    if (((MVMTracked *)tracked_in)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_oops(tc, "Can only use dispatcher-track-unbox-num on a tracked object");

    /* Resolve the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_in);

    /* Obtain the object and ensure it is concrete; also track its
     * type and concreteness since unboxing safety depends on this. */
    MVMObject *read_from = ((MVMTracked *)tracked_in)->body.value.o;
    if (!IS_CONCRETE(read_from))
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-unbox-num on a concrete object");
    record->rec.values[value_index].guard_type = 1;
    record->rec.values[value_index].guard_concreteness = 1;

    /* Read the value. */
    MVMRegister attr_value;
    attr_value.n64 = MVM_repr_get_num(tc, read_from);

    /* Ensure that we have this value read in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_unbox_num(tc, &(record->rec), value_index);
    if (!record->rec.values[result_value_index].tracked)
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
                attr_value, MVM_CALLSITE_ARG_NUM);
    return record->rec.values[result_value_index].tracked;
}

MVMObject * MVM_disp_program_record_track_unbox_str(MVMThreadContext *tc, MVMObject *tracked_in) {
    /* Ensure the tracked value is an object type. */
    if (((MVMTracked *)tracked_in)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_oops(tc, "Can only use dispatcher-track-unbox-str on a tracked object");

    /* Resolve the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_in);

    /* Obtain the object and ensure it is concrete; also track its
     * type and concreteness since unboxing safety depends on this. */
    MVMObject *read_from = ((MVMTracked *)tracked_in)->body.value.o;
    if (!IS_CONCRETE(read_from))
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-unbox-str on a concrete object");
    record->rec.values[value_index].guard_type = 1;
    record->rec.values[value_index].guard_concreteness = 1;

    /* Read the value. */
    MVMRegister attr_value;
    attr_value.s = MVM_repr_get_str(tc, read_from);

    /* Ensure that we have this value read in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_unbox_str(tc, &(record->rec), value_index);
    if (!record->rec.values[result_value_index].tracked)
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
                attr_value, MVM_CALLSITE_ARG_STR);
    return record->rec.values[result_value_index].tracked;
}

/* Start tracking the HOW of the input tracked object. This does not guard on
 * the type of the incoming tracked object, otherwise we'd not need this at
 * all (since that behavior can be obtained by doing a guard, calling .HOW,
 * and inserting it as a literal). */
MVMObject * MVM_disp_program_record_track_how(MVMThreadContext *tc, MVMObject *tracked_in) {
    /* Ensure the tracked value is an object type. */
    if (((MVMTracked *)tracked_in)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Can only use dispatcher-track-how on a tracked object");

    /* Resolve the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_in);

    /* Ensure that we have this HOW read in the values table, and make a
     * tracked object if not. */
    MVMuint32 result_value_index = value_index_how(tc, &(record->rec), value_index);
    if (!record->rec.values[result_value_index].tracked) {
        MVMRegister attr_value = { .o = STABLE(((MVMTracked *)tracked_in)->body.value.o)->HOW };
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
                attr_value, MVM_CALLSITE_ARG_OBJ);
    }
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

/* Record a guard of the HLL of the specified tracked value. */
void MVM_disp_program_record_guard_hll(MVMThreadContext *tc, MVMObject *tracked) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);
    record->rec.values[value_index].guard_hll = 1;
}

/* Add a lookup table as a constant value, and then record a lookup of a key in
 * it. Produces a new tracked value as a consequence. */
MVMObject * MVM_disp_program_record_index_lookup_table(MVMThreadContext *tc,
       MVMObject *lookup_hash, MVMObject *tracked_key) {
    /* Ensure the tracked value is a string and do the lookup. */
    if (!(((MVMTracked *)tracked_key)->body.kind & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Dispatch program lookup key must be a tracked string");
    MVMObject *resolved = MVM_repr_at_key_o(tc, lookup_hash,
            ((MVMTracked *)tracked_key)->body.value.s);

    /* Add the lookup hash as a constant value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMRegister lookup_register = { .o = lookup_hash };
    MVMuint32 lookup_index = value_index_constant(tc, &(record->rec), MVM_CALLSITE_ARG_OBJ,
        lookup_register);

    /* Resolve the tracked key. */
    MVMuint32 key_index = find_tracked_value_index(tc, &(record->rec), tracked_key);

    /* Ensure that we have this lookup in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_lookup(tc, &(record->rec), lookup_index,
        key_index);
    if (!record->rec.values[result_value_index].tracked) {
        MVMRegister resolved_register = { .o = resolved };
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
            resolved_register, MVM_CALLSITE_ARG_OBJ);
    }
    return record->rec.values[result_value_index].tracked;
}

/* Record a lookup of a key in a tracked value that should have representation
 * MVMHash. Type and cocreteness guards are added to enforce that. */
MVMObject * MVM_disp_program_record_index_tracked_lookup_table(MVMThreadContext *tc,
       MVMObject *tracked_lookup_hash, MVMObject *tracked_key) {
    /* Ensure the tracked lookup hash is a hash and the tracked value is a string,
     * and do the lookup. */
    if (!(((MVMTracked *)tracked_lookup_hash)->body.kind & MVM_CALLSITE_ARG_OBJ))
        MVM_exception_throw_adhoc(tc, "Dispatch program lookup hash must be a tracked object");
    MVMObject *lookup_hash = ((MVMTracked *)tracked_lookup_hash)->body.value.o;
    if (!IS_CONCRETE(lookup_hash) || !(REPR(lookup_hash)->ID == MVM_REPR_ID_MVMHash))
        MVM_exception_throw_adhoc(tc, "Dispatch program lookup hash must be a concrete VMHash");
    if (!(((MVMTracked *)tracked_key)->body.kind & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Dispatch program lookup key must be a tracked string");
    MVMObject *resolved = MVM_repr_at_key_o(tc, lookup_hash,
            ((MVMTracked *)tracked_key)->body.value.s);

    /* Resolve the tracked lookup and key. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 lookup_index = find_tracked_value_index(tc, &(record->rec), tracked_lookup_hash);
    MVMuint32 key_index = find_tracked_value_index(tc, &(record->rec), tracked_key);

    /* Enforce type and concreteness guards on the lookup table. */
    record->rec.values[lookup_index].guard_type = 1;
    record->rec.values[lookup_index].guard_concreteness = 1;

    /* Ensure that we have this lookup in the values table, and make
     * a tracked object if not. */
    MVMuint32 result_value_index = value_index_lookup(tc, &(record->rec), lookup_index,
        key_index);
    if (!record->rec.values[result_value_index].tracked) {
        MVMRegister resolved_register = { .o = resolved };
        record->rec.values[result_value_index].tracked = MVM_tracked_create(tc,
            resolved_register, MVM_CALLSITE_ARG_OBJ);
    }
    return record->rec.values[result_value_index].tracked;
}

/* Record that we drop arguments from a capture. Also perform the drops,
 * resulting in a new capture without that argument. */
MVMObject * MVM_disp_program_record_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 idx) {
    return MVM_disp_program_record_capture_drop_args(tc, capture, idx, 1);
}
MVMObject * MVM_disp_program_record_capture_drop_args(MVMThreadContext *tc, MVMObject *capture,
        MVMuint32 idx, MVMuint32 count) {
    /* Lookup the path to the incoming capture. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);

    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Calculate the new capture and add the necessary records for it.
     * We store one recording entry per dropped argument, but only the
     * final entry actually has a MVMCapture allocated for it.
     */
    MVMObject *new_capture = MVM_capture_drop_args(tc, capture, idx, count);
    for (MVMuint32 dropped_offs = 0; dropped_offs < count; dropped_offs++) {
        MVMDispProgramRecordingCapture new_capture_record = {
            .capture = dropped_offs == count - 1 ? new_capture : NULL,
            .transformation = MVMDispProgramRecordingDrop,
            .index = idx
        };

        MVM_VECTOR_INIT(new_capture_record.captures, 0);
        MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
        MVM_VECTOR_PUSH(update->captures, new_capture_record);
        MVM_VECTOR_PUSH(p.path, &update->captures[MVM_VECTOR_ELEMS(update->captures) - 1]);
    }

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

/* Record that we insert a tracked value into a capture. Also perform the insert
 * on the value that was read. */
MVMObject * MVM_disp_program_record_capture_replace_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 idx, MVMObject *tracked) {
    /* Lookup the index of the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked);

    /* Also look up the path to the incoming capture. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* First, create an entry as if we had dropped the argument.
     * We save the work of creating the capture here, because it is not
     * used by anything - it is anonymous and can not be addressed. */
    MVMDispProgramRecordingCapture dropped_arg_record = {
        .capture = NULL,
        .transformation = MVMDispProgramRecordingDrop,
        .index = idx,
    };
    MVM_VECTOR_INIT(dropped_arg_record.captures, 0);
    MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, dropped_arg_record);
    MVM_VECTOR_PUSH(p.path, &update->captures[MVM_VECTOR_ELEMS(update->captures) - 1]);

    MVMTracked *trackobj = (MVMTracked *)tracked;
    MVMObject *new_capture = MVM_capture_replace_arg(tc, capture, idx, trackobj->body.kind, trackobj->body.value);

    /* After that, create an entry as if we had just added an argument.
     * This one also gets to have the actual capture. */
    MVMDispProgramRecordingCapture new_capture_record = {
        .capture = new_capture,
        .transformation = MVMDispProgramRecordingInsert,
        .index = idx,
        .value_index = value_index
    };
    MVM_VECTOR_INIT(new_capture_record.captures, 0);
    update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, new_capture_record);
    MVM_VECTOR_DESTROY(p.path);

    /* Evaluate to the new capture, for the running dispatch function. */
    return new_capture;
}

/* Record that we insert a tracked value into a capture. Also perform the insert
 * on the value that was read. */
MVMObject * MVM_disp_program_record_capture_replace_literal_arg(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 idx, MVMCallsiteFlags kind, MVMRegister value) {
    /* Lookup the index of the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);

    /* Also look up the path to the incoming capture. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Obtain a new value index for the constant. */
    MVMuint32 value_index = value_index_constant(tc, &(record->rec), kind, value);

    /* First, create an entry as if we had dropped the argument.
     * We save the work of creating the capture here, because it is not
     * used by anything - it is anonymous and can not be addressed. */
    MVMDispProgramRecordingCapture dropped_arg_record = {
        .capture = NULL,
        .transformation = MVMDispProgramRecordingDrop,
        .index = idx,
    };
    MVM_VECTOR_INIT(dropped_arg_record.captures, 1);
    MVMDispProgramRecordingCapture *update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
    MVM_VECTOR_PUSH(update->captures, dropped_arg_record);
    MVM_VECTOR_PUSH(p.path, &update->captures[MVM_VECTOR_ELEMS(update->captures) - 1]);

    MVMObject *new_capture = MVM_capture_replace_arg(tc, capture, idx, kind, value);

    /* After that, create an entry as if we had just added an argument.
     * This one also gets to have the actual capture. */
    MVMDispProgramRecordingCapture new_capture_record = {
        .capture = new_capture,
        .transformation = MVMDispProgramRecordingInsert,
        .index = idx,
        .value_index = value_index
    };
    MVM_VECTOR_INIT(new_capture_record.captures, 0);
    update = p.path[MVM_VECTOR_ELEMS(p.path) - 1];
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

/* Check if an argument in a capture is a literal, either due to being flagged
 * that way in the callsite, or because it was a literal value that was
 * inserted into the capture. */
MVMint64 MVM_disp_program_record_capture_is_arg_literal(MVMThreadContext *tc,
        MVMObject *capture, MVMuint32 index) {
    /* Obtain the value from the capture to ensure it is in range. */
    MVMRegister value;
    MVMCallsiteFlags kind;
    MVM_capture_arg(tc, capture, index, &value, &kind);

    /* Ensure the incoming capture is known and calculate the path. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, capture, &p);

    /* Walk the capture path to see where the argument came from. */
    MVMint32 i;
    MVMuint32 real_index = index;
    for (i = MVM_VECTOR_ELEMS(p.path) - 1; i >= 0; i--) {
        switch (p.path[i]->transformation) {
            case MVMDispProgramRecordingInsert:
                /* It's an insert. Was the insert at the index we are dealing
                 * with? */
                if (p.path[i]->index == real_index) {
                    /* Yes, and so it will have a value index. We can then use
                     * that to see if it's literal. */
                    MVMuint32 idx = p.path[i]->value_index;
                    MVM_VECTOR_DESTROY(p.path);
                    return record->rec.values[idx].source == MVMDispProgramRecordingLiteralValue;
                }
                else {
                    /* No, so we may need to adjust the offset. */
                    if (real_index > p.path[i]->index)
                        real_index--;
                }
                break;
            case MVMDispProgramRecordingDrop:
                /* Adjust the index to account for the drop. */
                if (real_index >= p.path[i]->index)
                    real_index++;
                break;
            case MVMDispProgramRecordingInitial: {
                /* We have reached the initial capture, and so the index is
                 * for it. See if the callsite at this capture is literal. */
                MVM_VECTOR_DESTROY(p.path);
                MVMObject *init_capture = record->rec.initial_capture.capture;
                MVMCallsite *cs = ((MVMCapture *)init_capture)->body.callsite;
                return real_index < cs->flag_count &&
                    (cs->arg_flags[real_index] & MVM_CALLSITE_ARG_LITERAL);
            }
            default:
                break;
        }
    }
    MVM_VECTOR_DESTROY(p.path);

    /* If we didn't make a determination by this point, non-literal. */
    return 0;
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

/* Get the current resumption we're recording. */
static MVMDispProgramRecordingResumption * get_current_resumption(MVMThreadContext *tc,
        MVMCallStackDispatchRecord *record) {
    return &(record->rec.resumptions[MVM_VECTOR_ELEMS(record->rec.resumptions) - 1]);
}

/* Record the getting of the dispatch resume init args. */
MVMObject * MVM_disp_program_record_get_resume_init_args(MVMThreadContext *tc) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-get-resume-init-args in a resume callback");

    /* Hand back the capture, which was created at entry. */
    MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
    return resumption->initial_resume_capture.capture;
}

/* Set the resume state for the current dispatch resumption to a some already
 * tracked value. (Resume state is the stateful part that changes as the
 * dispatch progresses. We can only set it in the resumption handler, not in
 * the initial dispatch; that's what the resume init args are for). */
void MVM_disp_program_record_set_resume_state(MVMThreadContext *tc, MVMObject *tracked_obj) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-set-resume-state in a resume callback");

    /* Find the index of the tracked value, and also make sure that it is an
     * object type. */
    MVMuint32 value_index = find_tracked_value_index(tc, &(record->rec), tracked_obj);
    MVMTracked *tracked = (MVMTracked *)tracked_obj;
    if (tracked->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc,
            "Can only set an object state with dispatcher-set-resume-state");

    /* Register it for the sake of the dispatch program. */
    MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
    resumption->new_resume_state_value = value_index;

    /* Write the real resume state for the sake of the recording. */
    *(resumption->resume_state_ptr) = tracked->body.value.o;
}

/* Set the resume state to a literal object (which will become a dispatch
 * program constant). This allows caching of calculations - for example, method
 * deferral could calculate a linked list of methods to defer through. */
void MVM_disp_program_record_set_resume_state_literal(MVMThreadContext *tc, MVMObject *new_state) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-set-resume-state-literal in a resume callback");

    /* Add a dispatch program constant entry for the updated resume state,
     * and stash the index. */
    MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
    MVMRegister value = { .o = new_state };
    resumption->new_resume_state_value = value_index_constant(tc, &(record->rec),
            MVM_CALLSITE_ARG_OBJ, value);

    /* Write the real resume state. */
    *(resumption->resume_state_ptr) = new_state;
}

/* Get the resume state for the current dispatch resumption; returns a VMNull
 * if there is not yet any state set. */
MVMObject * MVM_disp_program_record_get_resume_state(MVMThreadContext *tc) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-get-resume-state in a resume callback");
    MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
    return *(resumption->resume_state_ptr);
}

/* Start tracking the resume state for the current dispatch resumption. This
 * allows guarding against it, or updating it in some simple calculated way
 * (for example, by getting an attribute of it). */
MVMObject * MVM_disp_program_record_track_resume_state(MVMThreadContext *tc) {
    /* Make sure we're in a dispatcher and that we're in a resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.resume_kind == MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc,
            "Can only use dispatcher-get-resume-state in a resume callback");

    /* Ensure we have a value index for the resume state, and create a tracking
     * wrapper if needed. */
    MVMuint32 value_index = value_index_resume_state(tc, &(record->rec));
    if (!record->rec.values[value_index].tracked) {
        MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
        MVMRegister value = { .o = *(resumption->resume_state_ptr) };
        record->rec.values[value_index].tracked = MVM_tracked_create(tc,
                value, MVM_CALLSITE_ARG_OBJ);
    }
    return record->rec.values[value_index].tracked;
}

/* Ensure we're in a state where running the resume dispatcher is OK. */
static void ensure_resume_ok(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    if (record->rec.resume_kind != MVMDispProgramRecordingResumeNone)
        MVM_exception_throw_adhoc(tc, "Can only enter a resumption once in a dispatch");
}

/* Form a capture from the resume initialization arguments. */
static MVMObject * resume_init_capture(MVMThreadContext *tc, MVMDispResumptionData *resume_data,
        MVMDispProgramRecordingResumption *rec_resumption) {
    MVMDispProgramResumption *resumption = resume_data->resumption;
    MVMCallsite *callsite = resumption->init_callsite;
    rec_resumption->initial_resume_args = callsite->flag_count
            ? MVM_malloc(callsite->flag_count * sizeof(MVMRegister))
            : NULL;
    for (MVMuint16 i = 0; i < callsite->flag_count; i++)
        rec_resumption->initial_resume_args[i] = MVM_disp_resume_get_init_arg(tc,
                resume_data, i);
    MVMArgs arg_info = {
        .callsite = callsite,
        .source = rec_resumption->initial_resume_args,
        .map = MVM_args_identity_map(tc, callsite)
    };
    tc->mark_args = &arg_info;
    MVMObject *capture = MVM_capture_from_args(tc, arg_info);
    tc->mark_args = NULL;
    return capture;
}

/* Set up another level of dispatch resumption in the resumptions list. Used
 * for both the initial resume and falling back on the next resumption. */
static void push_resumption(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        MVMDispResumptionData *resume_data) {
    MVMDispProgramRecordingResumption rec_resumption;
    rec_resumption.initial_resume_capture.transformation = MVMDispProgramRecordingResumeInitial;
    rec_resumption.initial_resume_capture.capture = resume_init_capture(tc, resume_data,
            &rec_resumption);
    MVM_VECTOR_INIT(rec_resumption.initial_resume_capture.captures, 4);
    rec_resumption.initial_resume_capture.transformation = MVMDispProgramRecordingResumeInitial;
    rec_resumption.resumption = resume_data->resumption;
    rec_resumption.resume_state_ptr = resume_data->state_ptr;
    rec_resumption.new_resume_state_value = -1;
    rec_resumption.num_values = 0;
    rec_resumption.num_resume_inits = 0;
    rec_resumption.no_next_resumption = 0;
    MVM_VECTOR_PUSH(record->rec.resumptions, rec_resumption);
}

/* Record the initial resumption of a dispatch. */
static void record_resume(MVMThreadContext *tc, MVMObject *capture, MVMDispResumptionData *resume_data,
        MVMDispProgramRecordingResumeKind resume_kind) {
    /* Make sure we're in a dispatcher and that we didn't already call resume. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_resume_ok(tc, record);
    ensure_known_capture(tc, record, capture);

    /* Set up the resumptions list and populate the initial entry (list as we
     * may fall back to resumptions of enclosing dispatchers). */
    MVM_VECTOR_INIT(record->rec.resumptions, 1);
    MVMROOT(tc, capture, {
        push_resumption(tc, record, resume_data);
    });

    /* Record the kind of dispatch resumption we're doing, and then delegate to
     * the appropriate `resume` dispatcher callback. */
    record->rec.resume_kind = resume_kind;
    record->outcome.kind = MVM_DISP_OUTCOME_RESUME;
    record->outcome.resume_capture = capture;
}

/* Report inability to resume dispatch, either by delegating to a language
 * configured dispatcher to do it, or throwing an exception if none is
 * configured. */
static void resume_error(MVMThreadContext *tc, MVMObject *capture) {
    MVMHLLConfig *hll = MVM_hll_current(tc);
    if (hll->resume_error_dispatcher)
        MVM_disp_program_record_delegate(tc, hll->resume_error_dispatcher, capture);
    else
        MVM_exception_throw_adhoc(tc, "No resumable dispatch in dynamic scope");
}

/* Record the resumption of the topmost dispatch. */
void MVM_disp_program_record_resume(MVMThreadContext *tc, MVMObject *capture) {
    MVMDispResumptionData resume_data;
    if (MVM_disp_resume_find_topmost(tc, &resume_data, 0))
        record_resume(tc, capture, &resume_data, MVMDispProgramRecordingResumeTopmost);
    else
        resume_error(tc, capture);
}

/* Record the resumption of a dispatch found relative to our caller. */
void MVM_disp_program_record_resume_caller(MVMThreadContext *tc, MVMObject *capture) {
    MVMDispResumptionData resume_data;
    if (MVM_disp_resume_find_caller(tc, &resume_data, 0))
        record_resume(tc, capture, &resume_data, MVMDispProgramRecordingResumeCaller);
    else
        resume_error(tc, capture);
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
    record->outcome.kind = MVM_DISP_OUTCOME_EXPECT_DELEGATE;
    record->outcome.delegate_disp = disp;
    record->outcome.delegate_capture = capture;
}

/* Record a delegation to the next resumption in this dispatch, assuming that
 * there is one. */
MVMint32 MVM_disp_program_record_next_resumption(MVMThreadContext *tc, MVMObject *with_args) {
    /* Make sure we're in a dispatcher and that we're in a resume, and try to find
     * the next level dispatch. Return zero if there is none. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMDispResumptionData resume_data;
    MVMuint32 have_next_resume = 0;
    switch (record->rec.resume_kind) {
        case MVMDispProgramRecordingResumeTopmost:
            have_next_resume = MVM_disp_resume_find_topmost(tc, &resume_data,
                    MVM_VECTOR_ELEMS(record->rec.resumptions));
            break;
        case MVMDispProgramRecordingResumeCaller:
            have_next_resume = MVM_disp_resume_find_caller(tc, &resume_data,
                    MVM_VECTOR_ELEMS(record->rec.resumptions));
            break;
        default:
            MVM_exception_throw_adhoc(tc,
                "Can only use dispatcher-next-resumption in a resume callback");
    }
    if (!have_next_resume) {
        MVMDispProgramRecordingResumption *resumption = get_current_resumption(tc, record);
        resumption->no_next_resumption = 1;
        return 0;
    }

    /* Record the dispatch action is to continue with the next resumption and
     * return a true value. (For certainty, we shall re-resolve the next
     * resumption when we actually process the outcome, since it's likely
     * possible for userspace code to do something terrible and invalidate
     * the held state.) */
    record->outcome.kind = MVM_DISP_OUTCOME_NEXT_RESUMPTION;
    record->outcome.resume_capture = with_args;
    return 1;
}

/* Record that if this dispatch program invokes something, and it fails to
 * bind, we want that to map to a dispatch resumption, not to an invocation
 * of the bind failure handler. */
void MVM_disp_program_record_resume_on_bind_failure(MVMThreadContext *tc, MVMuint32 flag) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.map_bind_outcome_to_resumption != MVMDispProgramRecordingBindControlNone)
        MVM_exception_throw_adhoc(tc, "Already configured bind control for this disaptch");
    record->rec.map_bind_outcome_to_resumption = MVMDispProgramRecordingBindControlFailure;
    record->rec.bind_failure_resumption_flag = flag;
}

/* Record that if this dispatch program invokes something, we only want it to
 * run up until a signature binding outcome is determined. */
void MVM_disp_program_record_resume_after_bind(MVMThreadContext *tc, MVMuint32 failure_flag,
        MVMuint32 success_flag) {
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    if (record->rec.map_bind_outcome_to_resumption != MVMDispProgramRecordingBindControlNone)
        MVM_exception_throw_adhoc(tc, "Already configured bind control for this disaptch");
    record->rec.map_bind_outcome_to_resumption = MVMDispProgramRecordingBindControlAll;
    record->rec.bind_failure_resumption_flag = failure_flag;
    record->rec.bind_success_resumption_flag = success_flag;
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
        case MVM_CALLSITE_ARG_UINT: record->outcome.result_kind = MVM_reg_uint64; break;
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
        case MVM_CALLSITE_ARG_UINT: record->outcome.result_kind = MVM_reg_uint64; break;
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
    MVM_callsite_intern(tc, &callsite, 0, 0);
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

void MVM_disp_program_record_foreign_code_constant(MVMThreadContext *tc, MVMNativeCall *result, MVMObject *capture) {
    /* Record the result action. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    ensure_known_capture(tc, record, capture);
    MVMRegister value = { .o = (MVMObject *)result };
    record->rec.outcome_value = value_index_constant(tc, &(record->rec),
            MVM_CALLSITE_ARG_OBJ, value);
    record->rec.outcome_capture = capture;

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_FOREIGNCODE;
    record->outcome.site = result;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Record a program terminator that invokes bytecode from a tracked value (for
 * example, from a capture or attribute read). Guards are established against
 * the tracked value for both type and concreteness as a side-effect. */
void MVM_disp_program_record_tracked_code(MVMThreadContext *tc, MVMObject *tracked,
        MVMObject *capture) {
    /* Look up the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 code_index = find_tracked_value_index(tc, &(record->rec), tracked);

    /* Ensure it is a concrete MVMCode object and establish guards. */
    if (((MVMTracked *)tracked)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Can only record tracked code result with an object");
    MVMObject *code = ((MVMTracked *)tracked)->body.value.o;
    if (REPR(code)->ID != MVM_REPR_ID_MVMCode || !IS_CONCRETE(code))
        MVM_exception_throw_adhoc(tc, "Can only record tracked code result with concrete MVMCode");
    MVM_disp_program_record_guard_type(tc, tracked);
    MVM_disp_program_record_guard_concreteness(tc, tracked);

    /* Record the index of the tracked value along with the capture. */
    ensure_known_capture(tc, record, capture);
    record->rec.outcome_value = code_index;
    record->rec.outcome_capture = capture;

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    MVM_callsite_intern(tc, &callsite, 0, 0);
    record->outcome.kind = MVM_DISP_OUTCOME_BYTECODE;
    record->outcome.code = (MVMCode *)code;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Record a program terminator that invokes a C function from a tracked value
 * (for example, from a capture or attribute read). Guards are established
 * against the tracked value for both type and concreteness as a side-effect. */
void MVM_disp_program_record_tracked_c_code(MVMThreadContext *tc, MVMObject *tracked,
        MVMObject *capture) {
    /* Look up the tracked value. */
    MVMCallStackDispatchRecord *record = MVM_callstack_find_topmost_dispatch_recording(tc);
    MVMuint32 code_index = find_tracked_value_index(tc, &(record->rec), tracked);

    /* Ensure it is a concrete MVMCode object and establish guards. */
    if (((MVMTracked *)tracked)->body.kind != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Can only record tracked code result with an object");
    MVMObject *code = ((MVMTracked *)tracked)->body.value.o;
    if (REPR(code)->ID != MVM_REPR_ID_MVMCFunction || !IS_CONCRETE(code))
        MVM_exception_throw_adhoc(tc, "Can only record tracked code result with concrete MVMCFunction");
    MVM_disp_program_record_guard_type(tc, tracked);
    MVM_disp_program_record_guard_concreteness(tc, tracked);

    /* Record the index of the tracked value along with the capture. */
    ensure_known_capture(tc, record, capture);
    record->rec.outcome_value = code_index;
    record->rec.outcome_capture = capture;

    /* Set up the invoke outcome. */
    MVMCallsite *callsite = ((MVMCapture *)capture)->body.callsite;
    record->outcome.kind = MVM_DISP_OUTCOME_CFUNCTION;
    record->outcome.c_func = ((MVMCFunction *)code)->body.func;
    record->outcome.args.callsite = callsite;
    record->outcome.args.map = MVM_args_identity_map(tc, callsite);
    record->outcome.args.source = ((MVMCapture *)capture)->body.args;
}

/* Process a recorded program. */
typedef struct {
    MVMuint32 temp_idx;
    MVMRegister value;
} fake_temp;
typedef struct {
    MVMDispProgramRecording *rec;
    MVM_VECTOR_DECL(MVMCollectable *, gc_constants);
    MVM_VECTOR_DECL(MVMDispProgramConstant, constants);
    MVM_VECTOR_DECL(MVMDispProgramOp, ops);
    /* A map of the temporaries, and which values they hold. This only covers
     * those involved in executing the guard program, not in building up a
     * final result capture. For now, we don't try and re-use temporaries. */
    MVM_VECTOR_DECL(MVMDispProgramRecordingValue *, value_temps);
    /* Temporaries used for resume init state that need to be faked up as
     * if we ran the recorded dispatch program. */
    MVM_VECTOR_DECL(fake_temp, fake_temps);
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
static MVMuint32 add_program_constant_hll(MVMThreadContext *tc, compile_state *cs,
        MVMHLLConfig *hll) {
    MVMDispProgramConstant c = { .hll = hll };
    MVM_VECTOR_PUSH(cs->constants, c);
    return MVM_VECTOR_ELEMS(cs->constants) - 1;
}
static MVMuint32 add_program_gc_constant(MVMThreadContext *tc, compile_state *cs,
        MVMCollectable *value) {
    MVM_ASSERT_NOT_FROMSPACE(tc, value);
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
        case MVMDispProgramRecordingResumeInitCaptureValue:
            op.code = MVMDispOpcodeLoadResumeInitValue;
            op.load.idx = v->resume_capture.index;
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
                case MVM_CALLSITE_ARG_UINT:
                    op.code = MVMDispOpcodeLoadConstantInt;
                    op.load.idx = add_program_constant_int(tc, cs, v->literal.value.u64);
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
                case MVM_CALLSITE_ARG_UINT:
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
        case MVMDispProgramRecordingHOWValue: {
            /* We first need to make sure that we load the dependent value,
             * then add the op to read it. */
            op.code = MVMDispOpcodeLoadHOW;
            op.load.idx = get_temp_holding_value(tc, cs, v->how.from_value);
            break;
        }
        case MVMDispProgramRecordingUnboxValue: {
            /* We first need to make sure that we load the dependent value,
             * then add the op to read it. */
            switch (v->unbox.kind) {
                case MVM_CALLSITE_ARG_INT:
                    op.code = MVMDispOpcodeUnboxInt;
                    break;
                case MVM_CALLSITE_ARG_NUM:
                    op.code = MVMDispOpcodeUnboxNum;
                    break;
                case MVM_CALLSITE_ARG_STR:
                    op.code = MVMDispOpcodeUnboxStr;
                    break;
                default:
                    MVM_oops(tc, "Unhandled kind of unbox in recorded dispatch: %d", v->attribute.kind);
            }
            op.load.idx = get_temp_holding_value(tc, cs, v->unbox.from_value);
            break;
        }
        case MVMDispProgramRecordingLookupValue: {
            /* Get the lookup and key loaded into temporaries. */
            MVMuint32 lookup_temp = get_temp_holding_value(tc, cs, v->lookup.lookup_index);
            MVMuint32 key_temp = get_temp_holding_value(tc, cs, v->lookup.key_index);

            /* Load the lookup into the target temporary; the op to do the lookup
             * will overwrite it. */
            op.code = MVMDispOpcodeSet;
            op.load.idx = lookup_temp;
            MVM_VECTOR_PUSH(cs->ops, op);

            /* Then emit the op to do the lookup. */
            op.code = MVMDispOpcodeLookup;
            op.load.idx = key_temp;
            break;
        };
        case MVMDispProgramRecordingResumeStateValue:
            op.code = MVMDispOpcodeLoadResumeState;
            break;
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
                        MVM_capture_arg_o(tc, capture_obj, index));
                MVM_VECTOR_PUSH(cs->ops, op);
            }
            else {
                if (v->guard_type) {
                    MVMObject *value = MVM_capture_arg_o(tc, capture_obj, index);
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
                else {
                    if (v->guard_concreteness) {
                        MVMObject *value = MVM_capture_arg_o(tc, capture_obj, index);
                        MVMDispProgramOp op;
                        op.code = IS_CONCRETE(value)
                                ? MVMDispOpcodeGuardArgConc
                                : MVMDispOpcodeGuardArgTypeObject;
                        op.arg_guard.arg_idx = (MVMuint16)index;
                        MVM_VECTOR_PUSH(cs->ops, op);
                    }
                    if (v->guard_hll) {
                        MVMObject *value = MVM_capture_arg_o(tc, capture_obj, index);
                        MVMHLLConfig *hll = STABLE(value)->hll_owner;
                        MVMDispProgramOp op;
                        op.code = MVMDispOpcodeGuardArgHLL;
                        op.arg_guard.arg_idx = (MVMuint16)index;
                        op.arg_guard.checkee = add_program_constant_hll(tc, cs, hll);
                        MVM_VECTOR_PUSH(cs->ops, op);
                    }
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
        case MVM_CALLSITE_ARG_UINT:
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
static void emit_loaded_value_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v, MVMuint32 temp, MVMRegister value,
        MVMCallsiteFlags kind) {
    switch (kind & MVM_CALLSITE_ARG_TYPE_MASK) {
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
                else {
                    if (v->guard_concreteness) {
                        MVMDispProgramOp op;
                        op.code = IS_CONCRETE(value.o)
                                ? MVMDispOpcodeGuardTempConc
                                : MVMDispOpcodeGuardTempTypeObject;
                        op.temp_guard.temp = temp;
                        MVM_VECTOR_PUSH(cs->ops, op);
                    }
                    if (v->guard_hll) {
                        MVMHLLConfig *hll = STABLE(value.o)->hll_owner;
                        MVMDispProgramOp op;
                        op.code = MVMDispOpcodeGuardTempHLL;
                        op.temp_guard.temp = temp;
                        op.temp_guard.checkee = add_program_constant_hll(tc, cs, hll);
                        MVM_VECTOR_PUSH(cs->ops, op);
                    }
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
        case MVM_CALLSITE_ARG_UINT:
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
            MVM_oops(tc, "Unexpected callsite arg type in emit_loaded_value_guards");
    }
}
static void emit_resume_init_capture_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingResumption *rec_res, MVMDispProgramRecordingValue *v,
        MVMuint32 value_index) {
    MVMuint32 temp = get_temp_holding_value(tc, cs, value_index);
    MVMRegister value = ((MVMTracked *)v->tracked)->body.value;
    MVMuint32 index = v->capture.index;
    MVMCallsiteFlags cs_flag = rec_res->resumption->init_callsite->arg_flags[index];
    emit_loaded_value_guards(tc, cs, v, temp, value, cs_flag);
}
static void emit_attribute_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v, MVMuint32 value_index) {
    MVMuint32 temp = get_temp_holding_value(tc, cs, value_index);
    MVMRegister value = ((MVMTracked *)v->tracked)->body.value;
    emit_loaded_value_guards(tc, cs, v, temp, value, v->attribute.kind);
}
static void emit_lookup_or_how_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v, MVMuint32 value_index) {
    MVMuint32 temp = get_temp_holding_value(tc, cs, value_index);
    MVMRegister value = ((MVMTracked *)v->tracked)->body.value;
    emit_loaded_value_guards(tc, cs, v, temp, value, MVM_CALLSITE_ARG_OBJ);
}
static void emit_resume_state_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingValue *v, MVMuint32 value_index) {
    MVMuint32 temp = get_temp_holding_value(tc, cs, value_index);
    MVMRegister value = ((MVMTracked *)v->tracked)->body.value;
    emit_loaded_value_guards(tc, cs, v, temp, value, MVM_CALLSITE_ARG_OBJ);
}
static void emit_args_ops(MVMThreadContext *tc, MVMCallStackDispatchRecord *record,
        compile_state *cs, MVMuint32 callsite_idx) {
    /* Obtain the path to the capture we'll be invoking with. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, record->rec.outcome_capture, &p);

    /* Calculate the length of the untouched tail between the incoming capture
     * and the outcome capture. This is defined as the part of it left untouched
     * by any inserts and drops. We start by assuming all of it is untouched. */
    MVMCallsite *initial_callsite = ((MVMCapture *)cs->rec->initial_capture.capture)->body.callsite;
    MVMuint32 untouched_tail_length = initial_callsite->flag_count;
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(p.path); i++) {
        MVMCapture *cur_capture = (MVMCapture *)p.path[i]->capture;
        MVMint32 skipped_drops = 0;
        while (!cur_capture) {
            assert(p.path[i]->transformation == MVMDispProgramRecordingDrop);
            skipped_drops++;
            i++;
            assert(i < MVM_VECTOR_ELEMS(p.path));
            cur_capture = (MVMCapture *)p.path[i]->capture;
        }
        /* If the previous loop has run at all, then the entry on the path reached
         * must either be a Drop operation (like disp-drop-n-args generates) or an
         * Insert operation (like disp-replace-arg generates).
         */
        assert(skipped_drops == 0
                    || p.path[i]->transformation == MVMDispProgramRecordingDrop
                    || p.path[i]->transformation == MVMDispProgramRecordingInsert);
        MVMCallsite *cur_callsite = cur_capture->body.callsite;
        switch (p.path[i]->transformation) {
            case MVMDispProgramRecordingInsert: {
                /* Given:
                 *   arg1, arg2, arg3, arg4
                 * If we insert at index 2, then we get:
                 *   arg1, arg2, inserted, arg3, arg4
                 * So the untouched tail is length 2, or more generally,
                 * (capture length - (index + 1)).
                 *
                 * If there have been any skipped drops, those are subtracted
                 * straight from the locally untouched tail length. */
                MVMuint32 locally_untouched = cur_callsite->flag_count - (p.path[i]->index + 1) - skipped_drops;
                if (locally_untouched < untouched_tail_length)
                    untouched_tail_length = locally_untouched;
                break;
            }
            case MVMDispProgramRecordingDrop: {
                /* Given:
                 *   arg1, arg2, arg3, arg4
                 * If we drop arg2 (index 1), then we get:
                 *  arg1, arg3, arg4
                 * Thus the untouched tail is 2, generally (capture length - index).
                 *
                 * When we have skipped multiple for the same index:
                 *  arg1, arg2, arg3, arg4, arg5
                 * If we drop arg2 and arg3 (index 1 twice), then we get:
                 *  arg1, arg4, arg5
                 * Thus the untouched tail is 2, generally (capture length - (index + skipped))
                 * It is as if we had only dropped the last argument, which is at the index
                 *  index + skipped
                 */
                MVMuint32 locally_untouched = cur_callsite->flag_count -
                    (p.path[i]->index + skipped_drops);
                if (locally_untouched < untouched_tail_length)
                    untouched_tail_length = locally_untouched;
                break;
            }
            case MVMDispProgramRecordingInitial:
                /* This is the initial capture, so nothing to do. */
                break;
            case MVMDispProgramRecordingResumeInitial:
                /* It's actually based on the initial resume capture, so the
                 * args tail re-use optimization is impossible. */
                untouched_tail_length = 0;
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
            SetFromInitialCapture,
            SetFromResumeInitCapture
        } Action;
        typedef struct {
            Action action;
            MVMuint32 index;
        } ArgProduction;
        ArgProduction *arg_prod = MVM_malloc(num_to_produce * sizeof(ArgProduction));

        for (i = 0; i < num_to_produce; i++) {
            /* Work out the source of this arg in the capture. For the rationale
             * for this algorithm, see MVM_disp_program_record_track_arg. */
            MVMint32 j;
            MVMuint32 real_index = i;
            MVMint32 found_value_index = -1;
            MVMuint32 from_resume_init_capture = 0;
            for (j = MVM_VECTOR_ELEMS(p.path) - 1; j >= 0 && found_value_index < 0; j--) {
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
                    case MVMDispProgramRecordingResumeInitial:
                        from_resume_init_capture = 1;
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
            else if (from_resume_init_capture) {
                /* It's a load of a value from the resume initialization state. */
                arg_prod[i].action = SetFromResumeInitCapture;
                arg_prod[i].index = real_index;
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
            switch (arg_prod[i].action) {
                case SetFromTemporary:
                    op.code = MVMDispOpcodeSet;
                    break;
                case SetFromInitialCapture:
                    op.code = MVMDispOpcodeLoadCaptureValue;
                    break;
                case SetFromResumeInitCapture:
                    op.code = MVMDispOpcodeLoadResumeInitValue;
                    break;
            }
            op.load.temp = args_base_temp + i;
            op.load.idx = arg_prod[i].index;
            MVM_VECTOR_PUSH(cs->ops, op);
        }

        MVM_free(arg_prod);

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
static void add_resume_init_temp_to_fake(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingResumption *rec_res, MVMuint32 temp_idx,
        MVMuint32 init_arg_idx) {
    /* Make sure we didn't already add the argument to fake; it's possible we
     * will end up overwriting it with the wrong value (because it could have
     * previously been set from a resumption other than the one we are now
     * in). */
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(cs->fake_temps); i++)
        if (cs->fake_temps[i].temp_idx == temp_idx)
            return;

    /* Not found, so add it. */
    MVMRegister value;
    MVMCallsiteFlags unused;
    MVM_capture_arg_by_flag_index(tc, rec_res->initial_resume_capture.capture, init_arg_idx,
            &value, &unused);
    fake_temp fake = { .temp_idx = temp_idx, .value = value };
    MVM_VECTOR_PUSH(cs->fake_temps, fake);
}
static void add_lookup_temp_to_fake(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingResumption *rec_res, MVMuint32 temp_idx,
        MVMObject *lookup_value) {
    MVMRegister value = { .o = lookup_value };
    fake_temp fake = { .temp_idx = temp_idx, .value = value };
    MVM_VECTOR_PUSH(cs->fake_temps, fake);
}
static void produce_resumption_init_values(MVMThreadContext *tc, compile_state *cs,
        MVMCallStackDispatchRecord *record, MVMDispProgramRecordingResumption *rec_res,
        MVMDispProgramResumption *res, MVMCapture *init_capture) {
    /* Obtain the path to the intialization capture. */
    CapturePath p;
    MVM_VECTOR_INIT(p.path, 8);
    calculate_capture_path(tc, record, (MVMObject *)init_capture, &p);

    /* Allocate storage for the resumption init value sources according to
     * the callsite size. */
    MVMuint16 arg_count = init_capture->body.callsite->flag_count;
    res->init_values = MVM_malloc(arg_count * sizeof(MVMDispProgramResumptionInitValue));

    /* Go through the capture and source each value. */
    for (MVMuint16 i = 0; i < arg_count; i++) {
        MVMint32 j;
        MVMuint32 real_index = i;
        MVMint32 found_value_index = -1;
        MVMuint32 is_resume_init_capture = 0;
        for (j = MVM_VECTOR_ELEMS(p.path) - 1; j >= 0 && found_value_index < 0; j--) {
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
                case MVMDispProgramRecordingResumeInitial:
                    is_resume_init_capture = 1;
                    break;
            }
        }
        MVMDispProgramResumptionInitValue *init = &(res->init_values[i]);
        if (found_value_index >= 0) {
            /* It's a value; go by source; not all can be used for init state. */
            MVMDispProgramRecordingValue *value = &(record->rec.values[found_value_index]);
            switch (value->source) {
                case MVMDispProgramRecordingCaptureValue:
                    init->source = MVM_DISP_RESUME_INIT_ARG;
                    init->index = value->capture.index;
                    break;
                case MVMDispProgramRecordingResumeInitCaptureValue:
                    /* We want to resume an argument for resumption initialization by an outer
                     * dispatcher. While we try to avoid any dynamic cost for the common cases
                     * of setting up a resume init capture, here the bookkeeping to do to try
                     * and do that is probably going to be far more costly (and easier to get
                     * subtly wrong) than any copying cost, so instead we load values into
                     * temporaries and then refer to those. In this case, there already is
                     * a temporary for it, so we can maybe re-use it. */
                    init->source = MVM_DISP_RESUME_INIT_TEMP;
                    init->index = get_temp_holding_value(tc, cs, found_value_index);
                    add_resume_init_temp_to_fake(tc, cs, rec_res, init->index,
                        value->resume_capture.index);
                    break;
                case MVMDispProgramRecordingLookupValue:
                    /* As described above, but for lookup values. */
                    init->source = MVM_DISP_RESUME_INIT_TEMP;
                    init->index = get_temp_holding_value(tc, cs, found_value_index);
                    add_lookup_temp_to_fake(tc, cs, rec_res, init->index,
                        ((MVMTracked *)value->tracked)->body.value.o);
                    break;
                case MVMDispProgramRecordingLiteralValue:
                    switch (value->literal.kind) {
                        case MVM_CALLSITE_ARG_OBJ:
                        case MVM_CALLSITE_ARG_STR:
                            init->source = MVM_DISP_RESUME_INIT_CONSTANT_OBJ;
                            init->index = add_program_gc_constant(tc, cs,
                                    (MVMCollectable *)value->literal.value.o);
                            break;
                        case MVM_CALLSITE_ARG_INT:
                        case MVM_CALLSITE_ARG_UINT:
                            init->source = MVM_DISP_RESUME_INIT_CONSTANT_INT;
                            init->index = add_program_constant_int(tc, cs,
                                    value->literal.value.i64);
                            break;
                        case MVM_CALLSITE_ARG_NUM:
                            init->source = MVM_DISP_RESUME_INIT_CONSTANT_NUM;
                            init->index = add_program_constant_num(tc, cs,
                                    value->literal.value.n64);
                            break;
                        default:
                            MVM_oops(tc, "Unknown kind of literal value in recorded dispatch");
                    }
                    break;
                default:
                    MVM_VECTOR_DESTROY(p.path);
                    MVM_exception_throw_adhoc(tc,
                            "Resume init arguments can only come from an initial argument capture or be constants");
            }
        }
        else if (is_resume_init_capture) {
            /* Load the value into a temporary. See comment above in the value
             * case for rationale. */
            init->source = MVM_DISP_RESUME_INIT_TEMP;
            MVMDispProgramOp op;
            op.code = MVMDispOpcodeLoadResumeInitValue;
            init->index = op.load.temp = MVM_VECTOR_ELEMS(cs->value_temps);
            op.load.idx = real_index;
            MVM_VECTOR_PUSH(cs->ops, op);
            MVM_VECTOR_PUSH(cs->value_temps, NULL);
            add_resume_init_temp_to_fake(tc, cs, rec_res, init->index, real_index);
        }
        else {
            /* It's an initial argument. */
            init->source = MVM_DISP_RESUME_INIT_ARG;
            init->index = real_index;
        }
    }

    /* Cleanup. */
    MVM_VECTOR_DESTROY(p.path);
}
static void emit_resume_inits(MVMThreadContext *tc, compile_state *cs,
        MVMCallStackDispatchRecord *record, MVMDispProgramRecordingResumption *rec_res,
        MVMDispProgram *dp, MVMuint32 from_inc, MVMuint32 to_exc) {
    for (MVMuint32 insert = from_inc, source = to_exc - 1; insert < to_exc; insert++, source--) {
        MVMDispProgramResumption *res = &(dp->resumptions[insert]);
        MVMDispProgramRecordingResumeInit *res_init =
            &(record->rec.resume_inits[source]);
        res->disp = res_init->disp;
        MVMObject *init_capture = res_init->capture;
        res->init_callsite = ((MVMCapture *)init_capture)->body.callsite;
        if (!res->init_callsite->is_interned)
            MVM_callsite_intern(tc, &(res->init_callsite), 1, 0);
        if (init_capture != record->rec.initial_capture.capture)
            produce_resumption_init_values(tc, cs, record, rec_res, res,
                    (MVMCapture *)init_capture);
    }
}
static void emit_value_guards(MVMThreadContext *tc, compile_state *cs,
        MVMDispProgramRecordingResumption *rec_res, MVMuint32 from_inc, MVMuint32 to_exc) {
    for (MVMuint32 i = from_inc; i < to_exc; i++) {
        MVMDispProgramRecordingValue *v = &(cs->rec->values[i]);
        if (v->source == MVMDispProgramRecordingCaptureValue) {
            emit_capture_guards(tc, cs, v);
        }
        else if (v->source == MVMDispProgramRecordingResumeInitCaptureValue) {
            if (rec_res)
                emit_resume_init_capture_guards(tc, cs, rec_res, v, i);
            else
                MVM_oops(tc, "Should not be emitting resume init guards in a non-resume dispatch");
        }
        else if (v->source == MVMDispProgramRecordingAttributeValue) {
            emit_attribute_guards(tc, cs, v, i);
        }
        else if (v->source == MVMDispProgramRecordingLookupValue ||
                v->source == MVMDispProgramRecordingHOWValue) {
            emit_lookup_or_how_guards(tc, cs, v, i);
        }
        else if (v->source == MVMDispProgramRecordingResumeStateValue) {
            emit_resume_state_guards(tc, cs, v, i);
        }
    }
}
static void process_recording(MVMThreadContext *tc, MVMCallStackDispatchRecord *record) {
    /* Dump the recording if we're debugging. */
    dump_recording(tc, record);

    /* Initialize compilation state for dispatch program. */
    compile_state cs;
    cs.rec = &(record->rec);
    MVM_VECTOR_INIT(cs.ops, 8);
    MVM_VECTOR_INIT(cs.gc_constants, 0);
    MVM_VECTOR_INIT(cs.constants, 0);
    MVM_VECTOR_INIT(cs.value_temps, 0);
    MVM_VECTOR_INIT(cs.fake_temps, 0);
    cs.args_buffer_temps = 0;

    /* Allocate the dispatch program and space for any new resumptions that
     * it has set up for the future. */
    MVMDispProgram *dp = MVM_malloc(sizeof(MVMDispProgram));
    dp->num_resumptions = MVM_VECTOR_ELEMS(record->rec.resume_inits);
    dp->resumptions = dp->num_resumptions
            ? MVM_calloc(dp->num_resumptions, sizeof(MVMDispProgramResumption))
            : NULL;

    /* Resumptions need special attention. We can work our way through a set
     * of nested resumptions (for example, if we're in a wrapped method, then
     * there could be a wrap dispatcher that, when exhausted, causes us to
     * fall back to the outer method dispatcher resumption). We'll have some
     * guards against both, as well as resumption state operations. Thus we
     * need to work through all of the resumptions and process the guards that
     * relate to each of them. */
    MVMuint32 is_resume = record->rec.resume_kind != MVMDispProgramRecordingResumeNone;
    if (is_resume) {
        /* Ensure the final number of values and resume inits in the final
         * resumption is correct. */
        get_current_resumption(tc, record)->num_values = MVM_VECTOR_ELEMS(record->rec.values);
        get_current_resumption(tc, record)->num_resume_inits = MVM_VECTOR_ELEMS(record->rec.resume_inits);

        /* Go through the resumptions. */
        MVMuint32 start_value = 0;
        MVMuint32 start_resume_init = 0;
        for (MVMuint32 i = 0; i < MVM_VECTOR_ELEMS(record->rec.resumptions); i++) {
            /* Emit op to start the resumption or to move to the next one,
             * depending. */
            MVMDispProgramOp op;
            op.code = i == 0 ? MVMDispOpcodeStartResumption : MVMDispOpcodeNextResumption;
            MVM_VECTOR_PUSH(cs.ops, op);

            /* Emit instruction that asserts we have the correct kind of dispatcher
             * at the current resumption nesting depth. */
            MVMDispProgramRecordingResumption *rec_res = &(record->rec.resumptions[i]);
            op.code = record->rec.resume_kind == MVMDispProgramRecordingResumeTopmost
                ? MVMDispOpcodeResumeTopmost
                : MVMDispOpcodeResumeCaller;
            op.resume.disp = rec_res->resumption->disp;
            MVM_VECTOR_PUSH(cs.ops, op);

            /* Check the callsite of the initialization state is the correct one. */
            op.code = MVMDispOpcodeGuardResumeInitCallsite;
            op.resume_init_callsite.callsite_idx = add_program_constant_callsite(tc, &cs,
                    ((MVMCapture *)rec_res->initial_resume_capture.capture)->body.callsite);
            MVM_VECTOR_PUSH(cs.ops, op);

            /* Emit the value guards for this resume, and set the number of
             * values we've emitted as the start for the next iteration. */
            emit_value_guards(tc, &cs, rec_res, start_value, rec_res->num_values);
            start_value = rec_res->num_values;

            /* Also do the same for new resume inits. */
            emit_resume_inits(tc, &cs, record, rec_res, dp, start_resume_init, rec_res->num_resume_inits);
            start_resume_init = rec_res->num_resume_inits;

            /* Emit any update to the resume state. */
            if (rec_res->new_resume_state_value >= 0) {
                MVMuint32 temp = get_temp_holding_value(tc, &cs, rec_res->new_resume_state_value);
                MVMDispProgramOp op;
                op.code = MVMDispOpcodeUpdateResumeState;
                op.res_value.temp = temp;
                MVM_VECTOR_PUSH(cs.ops, op);
            }

            /* If we expect there to be no further resumption, emit the op to
             * assert that. */
            if (rec_res->no_next_resumption) {
                MVMDispProgramOp op;
                op.code = record->rec.resume_kind == MVMDispProgramRecordingResumeTopmost
                    ? MVMDispOpcodeGuardNoResumptionTopmost
                    : MVMDispOpcodeGuardNoResumptionCaller;
                MVM_VECTOR_PUSH(cs.ops, op);
            }
        }
    }
    else {
        /* It's not a resume, so we want to emit all of the values guards and
         * resume initializations. */
        emit_value_guards(tc, &cs, NULL, 0, MVM_VECTOR_ELEMS(record->rec.values));
        emit_resume_inits(tc, &cs, record, NULL, dp, 0, dp->num_resumptions);
    }

    /* If we need to map bind failures into a resume, emit the op that will
     * create the special frame to do that. This can only be used if we have
     * a bytecode invocation outcome. */
    if (record->rec.map_bind_outcome_to_resumption != MVMDispProgramRecordingBindControlNone) {
        if (record->outcome.kind != MVM_DISP_OUTCOME_BYTECODE)
            MVM_oops(tc, "Can only use dispatcher-resume-on-bind-failure or dispatch-resume-after-bind with a bytecode result");
        if (record->rec.map_bind_outcome_to_resumption == MVMDispProgramRecordingBindControlAll) {
            MVMDispProgramOp op;
            op.code = MVMDispOpcodeBindCompletionToResumption;
            op.bind_control_resumption.failure_flag = record->rec.bind_failure_resumption_flag;
            op.bind_control_resumption.success_flag = record->rec.bind_success_resumption_flag;
            MVM_VECTOR_PUSH(cs.ops, op);
        }
        else {
            MVMDispProgramOp op;
            op.code = MVMDispOpcodeBindFailureToResumption;
            op.bind_control_resumption.failure_flag = record->rec.bind_failure_resumption_flag;
            MVM_VECTOR_PUSH(cs.ops, op);
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
        case MVM_DISP_OUTCOME_FOREIGNCODE: {
            /* Make sure we load the invokee into a temporary before we go any
             * further. This is the last temporary we add before dealing with
             * args. Also put callsite into constant table. */
            MVMuint32 temp_invokee = get_temp_holding_value(tc, &cs, record->rec.outcome_value);
            MVMuint32 callsite_idx = add_program_constant_callsite(tc, &cs,
                    ((MVMCapture *)record->rec.outcome_capture)->body.callsite);

            /* Produce the args op(s), and then add the dispatch op. */
            emit_args_ops(tc, record, &cs, callsite_idx);
            MVMDispProgramOp op;
            op.code = MVMDispOpcodeResultForeignCode;
            op.res_code.temp_invokee = temp_invokee;
            MVM_VECTOR_PUSH(cs.ops, op);
            break;
        }
        default:
            MVM_oops(tc, "Unimplemented dispatch outcome compilation");
    }

    /* Populate the rest of the dispatch program description. */
    dp->constants = cs.constants;
    dp->gc_constants = cs.gc_constants;
    dp->num_gc_constants = MVM_VECTOR_ELEMS(cs.gc_constants);
    dp->ops = cs.ops;
    dp->num_ops = MVM_VECTOR_ELEMS(cs.ops);
    dp->num_temporaries = MVM_VECTOR_ELEMS(cs.value_temps) + cs.args_buffer_temps;
    dp->first_args_temporary = MVM_VECTOR_ELEMS(cs.value_temps);

    /* Fake up any required temporaries. */
    if (MVM_VECTOR_ELEMS(cs.fake_temps)) {
        record->temps = MVM_calloc(dp->num_temporaries, sizeof(MVMRegister));
        for (MVMuint32 i = 0; i < MVM_VECTOR_ELEMS(cs.fake_temps); i++)
            record->temps[cs.fake_temps[i].temp_idx] = cs.fake_temps[i].value;
    }

    /* Clean up (we don't free most of the vectors because we've given them
     * over to the MVMDispProgram). */
    MVM_VECTOR_DESTROY(cs.value_temps);
    MVM_VECTOR_DESTROY(cs.fake_temps);

    /* Dump the program if we're debugging. */
    dump_program(tc, dp);

    /* Transition the inline cache to incorporate this dispatch program. */
    MVMuint32 installed = record->rec.do_not_install
        ? 0
        : MVM_disp_inline_cache_transition(tc, record->ic_entry_ptr,
            record->ic_entry, record->update_sf, record->initial_disp,
            ((MVMCapture *)record->rec.initial_capture.capture)->body.callsite,
            dp);

    /* We may need to keep the dispatch program around for the sake of any
     * resumptions. */
    if (dp->num_resumptions > 0) {
        record->produced_dp = dp;
        record->produced_dp_installed = installed;
    }

    /* If there's no resumptions and the transition failed, can destory the
     * dispatch program immediately. */
    else {
        if (!installed)
            MVM_disp_program_destroy(tc, dp);
    }
}

/* Called when we have finished recording a dispatch program. */
MVMuint32 MVM_disp_program_record_end(MVMThreadContext *tc, MVMCallStackDispatchRecord* record) {
    /* Set the result in place. */
    switch (record->outcome.kind) {
        case MVM_DISP_OUTCOME_FAILED:
            return 1;
        case MVM_DISP_OUTCOME_EXPECT_DELEGATE:
            if (record->outcome.delegate_disp)
                run_dispatch(tc, record, record->outcome.delegate_disp,
                        record->outcome.delegate_capture);
            else
                MVM_exception_throw_adhoc(tc, "Dispatch callback failed to delegate to a dispatcher");
            return 0;
        case MVM_DISP_OUTCOME_RESUME: {
            MVMDispProgramRecordingResumption *rec_resumption = get_current_resumption(tc, record);
            run_resume(tc, record, rec_resumption->resumption->disp,
                    record->outcome.resume_capture);
            return 0;
        }
        case MVM_DISP_OUTCOME_NEXT_RESUMPTION: {
            /* Find dispatch to resume and set up a resumption record for it. Then
             * run the resume callback. The capture is the same as the initial one
             * for this dispatch. */
            MVMDispResumptionData resume_data;
            MVMuint32 found = record->rec.resume_kind == MVMDispProgramRecordingResumeTopmost
                ? MVM_disp_resume_find_topmost(tc, &resume_data,
                    MVM_VECTOR_ELEMS(record->rec.resumptions))
                : MVM_disp_resume_find_caller(tc, &resume_data,
                    MVM_VECTOR_ELEMS(record->rec.resumptions));
            if (!found)
                MVM_exception_throw_adhoc(tc,
                    "Call stack inconsistency detected when moving to the next dispatch resumption");
            MVMDispProgramRecordingResumption *cur_res = get_current_resumption(tc, record);
            cur_res->num_values = MVM_VECTOR_ELEMS(record->rec.values);
            cur_res->num_resume_inits = MVM_VECTOR_ELEMS(record->rec.resume_inits);
            push_resumption(tc, record, &resume_data);
            MVMObject *args = record->outcome.resume_capture
                ? record->outcome.resume_capture
                : record->rec.initial_capture.capture;
            run_resume(tc, record, resume_data.resumption->disp, args);
            return 0;
        }
        case MVM_DISP_OUTCOME_VALUE: {
            process_recording(tc, record);
            MVMFrame *caller = find_calling_frame(tc, record->common.prev);
            caller->return_type = record->orig_return_type;
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
        case MVM_DISP_OUTCOME_BYTECODE: {
            MVMDispProgramRecordingBindControlKind bind_control_kind =
                record->rec.map_bind_outcome_to_resumption;
            MVMint64 bind_failure_resumption_flag = record->rec.bind_failure_resumption_flag;
            MVMint64 bind_success_resumption_flag = record->rec.bind_success_resumption_flag;
            process_recording(tc, record);
            MVM_disp_program_recording_destroy(tc, &(record->rec));
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = find_calling_frame(tc, tc->stack_top->prev);
            tc->cur_frame->return_type = record->orig_return_type;
            switch (bind_control_kind) {
                case MVMDispProgramRecordingBindControlFailure:
                    MVM_callstack_allocate_bind_control_failure_only(tc,
                        bind_failure_resumption_flag);
                    break;
                case MVMDispProgramRecordingBindControlAll:
                    MVM_callstack_allocate_bind_control(tc, bind_failure_resumption_flag,
                        bind_success_resumption_flag);
                    break;
                default:
                    break;
            }
            MVM_frame_dispatch(tc, record->outcome.code, record->outcome.args, -1);
            return 0;
        }
        case MVM_DISP_OUTCOME_CFUNCTION:
            process_recording(tc, record);
            MVM_disp_program_recording_destroy(tc, &(record->rec));
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = find_calling_frame(tc, tc->stack_top->prev);
            tc->cur_frame->return_type = record->orig_return_type;
            record->outcome.c_func(tc, record->outcome.args);
            return 1;
        case MVM_DISP_OUTCOME_FOREIGNCODE:
            process_recording(tc, record);
            MVM_disp_program_recording_destroy(tc, &(record->rec));
            record->common.kind = MVM_CALLSTACK_RECORD_DISPATCH_RECORDED;
            tc->cur_frame = find_calling_frame(tc, tc->stack_top->prev);
            tc->cur_frame->return_type = record->orig_return_type;

            MVMObject *site = (MVMObject *)record->outcome.site;
            MVMObject *result_type = record->outcome.args.source[record->outcome.args.map[0]].o;
            MVM_nativecall_dispatch(tc, result_type, site, record->outcome.args);
            if (tc->cur_frame->return_type == MVM_RETURN_OBJ && MVM_spesh_log_is_logging(tc))
                MVM_spesh_log_type(tc, tc->cur_frame->return_value->o);
            return 1;
        default:
            MVM_oops(tc, "Unimplemented dispatch program outcome kind");
    }
}

/* Interpret a dispatch program. */
#if MVM_CGOTO
#define DISPATCH(op)
#define OP(name) name
#define NEXT do { \
        op = dp->ops[i++];      \
        goto *LABELS[op.code];  \
    } while(0)
#else
#define DISPATCH(op) switch (op)
#define OP(name) case name
#define NEXT break
#endif
#define GET_ARG MVMRegister val = args->source[args->map[op.arg_guard.arg_idx]]
#define MAX_RES_STATES 8
MVMint64 MVM_disp_program_run(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallStackDispatchRun *record, MVMint32 spesh_cid, MVMuint32 bytecode_offset,
        MVMuint32 dp_index) {
#if MVM_CGOTO
#include "labels.h"
#endif

    /* Resume states should only be put into place once we commit the dispatch
     * program. */
    MVMObject **resume_state_targets[MAX_RES_STATES];
    MVMObject *resume_state_values[MAX_RES_STATES];
    MVMuint8 num_resume_states = 0;

    MVMArgs *args = &(record->arg_info);
    MVMuint32 i = 0 ;
    MVMArgs invoke_args;
    MVMDispProgramOp op;
#if !MVM_CGOTO
    while(i < dp->num_ops)
#endif
    {
#if !MVM_CGOTO
        op = dp->ops[i++];
#endif
        DISPATCH (op.code) {
#if MVM_CGOTO
               NEXT;
#endif
            /* Resumption related ops. */
            OP(MVMDispOpcodeStartResumption):
                record->resumption_level = 0;
                NEXT;
            OP(MVMDispOpcodeNextResumption):
                record->resumption_level++;
                NEXT;
            OP(MVMDispOpcodeResumeTopmost):
                if (!MVM_disp_resume_find_topmost(tc, &(record->resumption_data), record->resumption_level))
                    goto rejection;
                if (record->resumption_data.resumption->disp != op.resume.disp)
                    goto rejection;
                NEXT;
            OP(MVMDispOpcodeResumeCaller):
                if (!MVM_disp_resume_find_caller(tc, &(record->resumption_data), record->resumption_level))
                    goto rejection;
                if (record->resumption_data.resumption->disp != op.resume.disp)
                    goto rejection;
                NEXT;
            OP(MVMDispOpcodeGuardResumeInitCallsite): {
                MVMCallsite *expected = dp->constants[op.resume_init_callsite.callsite_idx].cs;
                if (record->resumption_data.resumption->init_callsite != expected)
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardNoResumptionTopmost):
                if (MVM_disp_resume_find_topmost(tc, &(record->resumption_data), record->resumption_level))
                    goto rejection;
		        NEXT;
            OP(MVMDispOpcodeGuardNoResumptionCaller):
                if (MVM_disp_resume_find_caller(tc, &(record->resumption_data), record->resumption_level))
                    goto rejection;
		        NEXT;
            OP(MVMDispOpcodeUpdateResumeState):
                if (num_resume_states >= MAX_RES_STATES)
                    MVM_exception_throw_adhoc(tc, "Too many resume state updates");
                resume_state_targets[num_resume_states] = record->resumption_data.state_ptr;
                resume_state_values[num_resume_states] = record->temps[op.res_value.temp].o;
                MVM_gc_root_temp_push(tc, (MVMCollectable**)&resume_state_values[num_resume_states]);
                num_resume_states++;
                NEXT;
            /* Argument guard ops. */
            OP(MVMDispOpcodeGuardArgType): {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.arg_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgTypeConc): {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.arg_guard.checkee]
                        || !IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgTypeTypeObject): {
                GET_ARG;
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.arg_guard.checkee]
                        || IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgConc): {
                GET_ARG;
                if (!IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgTypeObject): {
                GET_ARG;
                if (IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgLiteralObj): {
                GET_ARG;
                if (val.o != (MVMObject *)dp->gc_constants[op.arg_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgLiteralStr): {
                GET_ARG;
                if (!MVM_string_equal(tc, val.s, (MVMString *)dp->gc_constants[op.arg_guard.checkee]))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgLiteralInt): {
                GET_ARG;
                if (val.i64 != dp->constants[op.arg_guard.checkee].i64)
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgLiteralNum): {
                GET_ARG;
                if (val.n64 != dp->constants[op.arg_guard.checkee].n64)
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgNotLiteralObj): {
                GET_ARG;
                if (val.o == (MVMObject *)dp->gc_constants[op.arg_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardArgHLL): {
                GET_ARG;
                if (STABLE(val.o)->hll_owner != dp->constants[op.arg_guard.checkee].hll)
                    goto rejection;
                NEXT;
            }

            /* Temporary guard ops. */
            OP(MVMDispOpcodeGuardTempType): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.temp_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempTypeConc): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.temp_guard.checkee]
                        || !IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempTypeTypeObject): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (STABLE(val.o) != (MVMSTable *)dp->gc_constants[op.temp_guard.checkee]
                        || IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempConc): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (!IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempTypeObject): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (IS_CONCRETE(val.o))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempLiteralObj): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (val.o != (MVMObject *)dp->gc_constants[op.temp_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempLiteralStr): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (!MVM_string_equal(tc, val.s, (MVMString *)dp->gc_constants[op.temp_guard.checkee]))
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempLiteralInt): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (val.i64 != dp->constants[op.temp_guard.checkee].i64)
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempLiteralNum): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (val.n64 != dp->constants[op.temp_guard.checkee].n64)
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempNotLiteralObj): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (val.o == (MVMObject *)dp->gc_constants[op.temp_guard.checkee])
                    goto rejection;
                NEXT;
            }
            OP(MVMDispOpcodeGuardTempHLL): {
                MVMRegister val = record->temps[op.temp_guard.temp];
                if (STABLE(val.o)->hll_owner != dp->constants[op.temp_guard.checkee].hll)
                    goto rejection;
                NEXT;
            }

            /* Load ops. */
            OP(MVMDispOpcodeLoadCaptureValue):
                record->temps[op.load.temp] = args->source[args->map[op.load.idx]];
                NEXT;
            OP(MVMDispOpcodeLoadResumeInitValue):
                record->temps[op.load.temp] = MVM_disp_resume_get_init_arg(tc,
                        &(record->resumption_data), op.load.idx);
                NEXT;
            OP(MVMDispOpcodeLoadResumeState):
                record->temps[op.load.temp].o = *(record->resumption_data.state_ptr);
                NEXT;
            OP(MVMDispOpcodeLoadConstantObjOrStr):
                record->temps[op.load.temp].o = (MVMObject *)dp->gc_constants[op.load.idx];
                NEXT;
            OP(MVMDispOpcodeLoadConstantInt):
                record->temps[op.load.temp].i64 = dp->constants[op.load.idx].i64;
                NEXT;
            OP(MVMDispOpcodeLoadConstantNum):
                record->temps[op.load.temp].n64 = dp->constants[op.load.idx].n64;
                NEXT;
            OP(MVMDispOpcodeLoadAttributeObj): {
                MVMObject *o = MVM_p6opaque_read_object(tc,
                        record->temps[op.load.temp].o, op.load.idx);
                record->temps[op.load.temp].o = o ? o : tc->instance->VMNull;
                NEXT;
            }
            OP(MVMDispOpcodeLoadAttributeInt):
                record->temps[op.load.temp].i64 = MVM_p6opaque_read_int64(tc,
                        record->temps[op.load.temp].o, op.load.idx);
                NEXT;
            OP(MVMDispOpcodeLoadAttributeNum):
                record->temps[op.load.temp].n64 = MVM_p6opaque_read_num64(tc,
                        record->temps[op.load.temp].o, op.load.idx);
                NEXT;
            OP(MVMDispOpcodeLoadAttributeStr):
                record->temps[op.load.temp].s = MVM_p6opaque_read_str(tc,
                        record->temps[op.load.temp].o, op.load.idx);
                NEXT;
            OP(MVMDispOpcodeLoadHOW): {
                MVMObject *HOW = STABLE(record->temps[op.load.idx].o)->HOW;
                if (!HOW) /* Not deserialized */
                    goto rejection;
                record->temps[op.load.temp].o = HOW;
                NEXT;
            }
            OP(MVMDispOpcodeUnboxInt): {
                record->temps[op.load.temp].i64 = MVM_repr_get_int(tc, record->temps[op.load.idx].o);
                NEXT;
            }
            OP(MVMDispOpcodeUnboxNum): {
                record->temps[op.load.temp].n64 = MVM_repr_get_num(tc, record->temps[op.load.idx].o);
                NEXT;
            }
            OP(MVMDispOpcodeUnboxStr): {
                record->temps[op.load.temp].s = MVM_repr_get_str(tc, record->temps[op.load.idx].o);
                NEXT;
            }
            OP(MVMDispOpcodeLookup):
                record->temps[op.load.temp].o = MVM_repr_at_key_o(tc,
                        record->temps[op.load.temp].o,
                        record->temps[op.load.idx].s);
                NEXT;
            OP(MVMDispOpcodeSet):
                record->temps[op.load.temp] = record->temps[op.load.idx];
                NEXT;

            /* Value result ops. */
            OP(MVMDispOpcodeResultValueObj): {
                MVM_args_set_dispatch_result_obj(tc, tc->cur_frame,
                        record->temps[op.res_value.temp].o);
                MVM_callstack_unwind_dispatch_run(tc);
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);
                goto accept;
            }
            OP(MVMDispOpcodeResultValueStr): {
                MVM_args_set_dispatch_result_str(tc, tc->cur_frame,
                        record->temps[op.res_value.temp].s);
                MVM_callstack_unwind_dispatch_run(tc);
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);
                goto accept;
            }
            OP(MVMDispOpcodeResultValueInt): {
                MVM_args_set_dispatch_result_int(tc, tc->cur_frame,
                        record->temps[op.res_value.temp].i64);
                MVM_callstack_unwind_dispatch_run(tc);
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);
                goto accept;
            }
            OP(MVMDispOpcodeResultValueNum): {
                MVM_args_set_dispatch_result_num(tc, tc->cur_frame,
                        record->temps[op.res_value.temp].n64);
                MVM_callstack_unwind_dispatch_run(tc);
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);
                goto accept;
            }

            /* Bind control to resumption callstack record. */
            OP(MVMDispOpcodeBindFailureToResumption):
                MVM_callstack_allocate_bind_control_failure_only(tc,
                    op.bind_control_resumption.failure_flag);
                NEXT;
            OP(MVMDispOpcodeBindCompletionToResumption):
                MVM_callstack_allocate_bind_control(tc,
                    op.bind_control_resumption.failure_flag,
                    op.bind_control_resumption.success_flag);
                NEXT;

            /* Args preparation for invocation result. */
            OP(MVMDispOpcodeUseArgsTail):
                invoke_args.callsite = dp->constants[op.use_arg_tail.callsite_idx].cs;
                invoke_args.source = args->source;
                invoke_args.map = args->map + op.use_arg_tail.skip_args;
                NEXT;
            OP(MVMDispOpcodeCopyArgsTail): {
                invoke_args.callsite = dp->constants[op.copy_arg_tail.callsite_idx].cs;
                invoke_args.source = record->temps + dp->first_args_temporary;
                invoke_args.map = MVM_args_identity_map(tc, invoke_args.callsite);
                MVMuint32 to_copy = op.copy_arg_tail.tail_args;
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
                NEXT;
            }

            /* Invocation results. */
            OP(MVMDispOpcodeResultBytecode): {
                record->chosen_dp = dp;
                MVMCode *code = (MVMCode *)record->temps[op.res_code.temp_invokee].o;
                if (spesh_cid) {
                    MVMROOT(tc, code, {
                        MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                            bytecode_offset, dp_index);
                        if (tc->spesh_log) /* Log may have filled from previous entry */
                            MVM_spesh_log_bytecode_target(tc, spesh_cid, bytecode_offset, code);
                    });
                }
                MVM_frame_dispatch(tc, code, invoke_args, -1);
                goto accept;
            }
            OP(MVMDispOpcodeResultCFunction): {
                record->chosen_dp = dp;
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);
                MVMCFunction *wrapper = (MVMCFunction *)record->temps[op.res_code.temp_invokee].o;
                wrapper->body.func(tc, invoke_args);
                MVM_callstack_unwind_dispatch_run(tc);
                goto accept;
            }
            OP(MVMDispOpcodeResultForeignCode): {
                record->chosen_dp = dp;
                if (spesh_cid)
                    MVM_spesh_log_dispatch_resolution_for_correlation_id(tc, spesh_cid,
                        bytecode_offset, dp_index);

                MVMObject *result_type = invoke_args.source[invoke_args.map[0]].o;
                MVM_nativecall_dispatch(tc, result_type, record->temps[op.res_code.temp_invokee].o, invoke_args);
                if (tc->cur_frame->return_type == MVM_RETURN_OBJ && MVM_spesh_log_is_logging(tc))
                    MVM_spesh_log_type(tc, tc->cur_frame->return_value->o);
                MVM_callstack_unwind_dispatch_run(tc);
                goto accept;
            }
#if !MVM_CGOTO
            default:
                MVM_oops(tc, "Unknown dispatch program op %d", op.code);
#endif
        }
    }
    MVM_oops(tc, "Should not reach end of dispatch program without a result");
accept:
    MVM_gc_root_temp_pop_n(tc, num_resume_states);
    while (num_resume_states--) {
        *resume_state_targets[num_resume_states] = resume_state_values[num_resume_states];
    }
    return 1;
rejection:
    MVM_gc_root_temp_pop_n(tc, num_resume_states);
    return 0;
}

#define add_collectable(tc, worklist, snapshot, col, desc) \
    do { \
        if (worklist) { \
            MVM_gc_worklist_add(tc, worklist, &(col)); \
        } \
        else { \
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, \
                (MVMCollectable *)col, desc); \
        } \
    } while (0)

/* GC mark a dispatch program's GC constants. */
void MVM_disp_program_mark(MVMThreadContext *tc, MVMDispProgram *dp, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot) {
    MVMuint32 i;
    for (i = 0; i < dp->num_gc_constants; i++)
        add_collectable(tc, worklist, snapshot, dp->gc_constants[i],
                "Dispatch program GC constant");
}

/* Mark the recording state of a dispatch program. */
static void mark_recording_capture(MVMThreadContext *tc, MVMDispProgramRecordingCapture *cap,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    add_collectable(tc, worklist, snapshot, cap->capture, "Dispatch recording capture");
    MVMuint32 i;
    for (i = 0; i < MVM_VECTOR_ELEMS(cap->captures); i++)
        mark_recording_capture(tc, &(cap->captures[i]), worklist, snapshot);
}
void MVM_disp_program_mark_recording(MVMThreadContext *tc, MVMDispProgramRecording *rec,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMuint32 i, j;
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->values); i++) {
        MVMDispProgramRecordingValue *value = &(rec->values[i]);
        switch (value->source) {
            case MVMDispProgramRecordingCaptureValue:
            case MVMDispProgramRecordingResumeInitCaptureValue:
            case MVMDispProgramRecordingAttributeValue:
            case MVMDispProgramRecordingLookupValue:
            case MVMDispProgramRecordingHOWValue:
            case MVMDispProgramRecordingUnboxValue:
            case MVMDispProgramRecordingResumeStateValue:
                /* Nothing to mark. */
                break;
            case MVMDispProgramRecordingLiteralValue:
                if (value->literal.kind == MVM_CALLSITE_ARG_OBJ ||
                        value->literal.kind == MVM_CALLSITE_ARG_STR)
                    add_collectable(tc, worklist, snapshot, value->literal.value.o,
                            "Dispatch recording value");
                break;
            default:
                MVM_panic(1, "Unknown dispatch program value kind to GC mark");
                break;
        }
        add_collectable(tc, worklist, snapshot, value->tracked,
                "Dispatch recording tracked value");
        for (j = 0; j < MVM_VECTOR_ELEMS(value->not_literal_guards); j++)
            add_collectable(tc, worklist, snapshot, value->not_literal_guards[j],
                    "Dispatch recording literal non-match guard");
    }
    mark_recording_capture(tc, &(rec->initial_capture), worklist, snapshot);
    if (rec->resume_kind != MVMDispProgramRecordingResumeNone) {
        for (MVMuint32 i = 0; i < MVM_VECTOR_ELEMS(rec->resumptions); i++) {
            MVMDispProgramRecordingResumption *resumption = &(rec->resumptions[i]);
            mark_recording_capture(tc, &(resumption->initial_resume_capture), worklist, snapshot);
            if (resumption->initial_resume_args) {
                MVMCallsite *init_callsite = ((MVMCapture *)resumption->initial_resume_capture.capture)->body.callsite;
                for (MVMuint16 j = 0; j < init_callsite->flag_count; j++) {
                    MVMCallsiteFlags flag = init_callsite->arg_flags[j] & MVM_CALLSITE_ARG_TYPE_MASK;
                    if (flag == MVM_CALLSITE_ARG_OBJ || flag == MVM_CALLSITE_ARG_STR)
                        add_collectable(tc, worklist, snapshot,
                                resumption->initial_resume_args[j].o,
                                "Dispatch recording initial resume argument");
                }
            }
        }
    }
    for (i = 0; i < MVM_VECTOR_ELEMS(rec->resume_inits); i++) {
        add_collectable(tc, worklist, snapshot, rec->resume_inits[i].capture,
                "Dispatch recording resume initialization capture");
    }
    add_collectable(tc, worklist, snapshot, rec->outcome_capture,
            "Dispatch recording outcome capture");
}

/* Mark the temporaries of a running dispatch program. */
static void mark_resumption_temps(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMRegister *temps, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    for (MVMuint32 i = 0; i < dp->num_resumptions; i++) {
        MVMDispProgramResumptionInitValue *init_values = dp->resumptions[i].init_values;
        if (init_values) {
            MVMCallsite *cs = dp->resumptions[i].init_callsite;
            for (MVMuint32 j = 0; j < cs->flag_count; j++) {
                if (init_values[j].source == MVM_DISP_RESUME_INIT_TEMP) {
                    MVMCallsiteFlags flag = cs->arg_flags[j] & MVM_CALLSITE_ARG_TYPE_MASK;
                    if (flag == MVM_CALLSITE_ARG_OBJ || flag == MVM_CALLSITE_ARG_STR)
                        add_collectable(tc, worklist, snapshot, temps[init_values[j].index].o,
                                "Dispatch program temporary (resumption initialization)");
                }
            }
        }
    }
}
void MVM_disp_program_mark_run_temps(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMCallsite *cs, MVMRegister *temps, MVMGCWorklist *worklist,
        MVMHeapSnapshotState *snapshot) {
    if (dp->num_temporaries != dp->first_args_temporary) {
        /* Some of the temporaries form the result argument capture. */
        for (MVMuint32 i = 0; i < cs->flag_count; i++) {
            if (cs->arg_flags[i] & (MVM_CALLSITE_ARG_OBJ | MVM_CALLSITE_ARG_STR)) {
                MVMuint32 temp_idx = dp->first_args_temporary + i;
                add_collectable(tc, worklist, snapshot, temps[temp_idx].o,
                        "Dispatch program temporary (arg)");
            }
        }
    }
    mark_resumption_temps(tc, dp, temps, worklist, snapshot);
}

/* Mark the temporaries of a recorded dispatch program. */
void MVM_disp_program_mark_record_temps(MVMThreadContext *tc, MVMDispProgram *dp,
        MVMRegister *temps, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    mark_resumption_temps(tc, dp, temps, worklist, snapshot);
}

/* Mark the outcome of a dispatch program. */
void MVM_disp_program_mark_outcome(MVMThreadContext *tc, MVMDispProgramOutcome *outcome,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    switch (outcome->kind) {
        case MVM_DISP_OUTCOME_FAILED:
        case MVM_DISP_OUTCOME_CFUNCTION:
            /* Nothing to mark for these. */
            break;
        case MVM_DISP_OUTCOME_EXPECT_DELEGATE:
            add_collectable(tc, worklist, snapshot, outcome->delegate_capture,
                    "Dispatch outcome (delegate capture)");
            break;
        case MVM_DISP_OUTCOME_RESUME:
        case MVM_DISP_OUTCOME_NEXT_RESUMPTION:
            add_collectable(tc, worklist, snapshot, outcome->resume_capture,
                    "Dispatch outcome (resume capture)");
            break;
        case MVM_DISP_OUTCOME_VALUE:
            if (outcome->result_kind == MVM_reg_obj || outcome->result_kind == MVM_reg_str)
                add_collectable(tc, worklist, snapshot, outcome->result_value.o,
                        "Dispatch outcome (value)");
            break;
        case MVM_DISP_OUTCOME_BYTECODE:
            add_collectable(tc, worklist, snapshot, outcome->code,
                    "Dispatch outcome (bytecode)");
            break;
        case MVM_DISP_OUTCOME_FOREIGNCODE:
            add_collectable(tc, worklist, snapshot, outcome->site,
                    "Dispatch outcome (foreign function)");
            break;
    }
}

/* Release memory associated with a dispatch program. */
void MVM_disp_program_destroy(MVMThreadContext *tc, MVMDispProgram *dp) {
    MVM_free(dp->constants);
    MVM_free(dp->gc_constants);
    MVM_free(dp->ops);
    for (MVMuint32 i = 0; i < dp->num_resumptions; i++) {
        MVMDispProgramResumption *resumption = &(dp->resumptions[i]);
        if (resumption->init_values) {
            MVM_free(resumption->init_values);
        }
    }
    MVM_free(dp->resumptions);
    MVM_free(dp);
}

/* Free the memory associated with a dispatch program recording. */
static void destroy_recording_capture(MVMThreadContext *tc, MVMDispProgramRecordingCapture *cap) {
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
    if (rec->resume_kind != MVMDispProgramRecordingResumeNone) {
        for (i = 0; i < MVM_VECTOR_ELEMS(rec->resumptions); i++) {
            MVMDispProgramRecordingResumption *resumption = &(rec->resumptions[i]);
            if (resumption->initial_resume_args) {
                MVM_free(resumption->initial_resume_args);
            }
            destroy_recording_capture(tc, &(resumption->initial_resume_capture));
        }
        MVM_VECTOR_DESTROY(rec->resumptions);
    }
}

const char *MVM_disp_opcode_to_name(MVMDispProgramOpcode op) {
    switch (op) {
        case MVMDispOpcodeStartResumption: return "MVMDispOpcodeStartResumption";
        case MVMDispOpcodeNextResumption: return "MVMDispOpcodeNextResumption";
        case MVMDispOpcodeResumeTopmost: return "MVMDispOpcodeResumeTopmost";
        case MVMDispOpcodeResumeCaller: return "MVMDispOpcodeResumeCaller";
        case MVMDispOpcodeGuardResumeInitCallsite: return "MVMDispOpcodeGuardResumeInitCallsite";
        case MVMDispOpcodeGuardNoResumptionTopmost: return "MVMDispOpcodeGuardNoResumptionTopmost";
        case MVMDispOpcodeGuardNoResumptionCaller: return "MVMDispOpcodeGuardNoResumptionCaller";
        case MVMDispOpcodeUpdateResumeState: return "MVMDispOpcodeUpdateResumeState";
        case MVMDispOpcodeGuardArgType: return "MVMDispOpcodeGuardArgType";
        case MVMDispOpcodeGuardArgTypeConc: return "MVMDispOpcodeGuardArgTypeConc";
        case MVMDispOpcodeGuardArgTypeTypeObject: return "MVMDispOpcodeGuardArgTypeTypeObject";
        case MVMDispOpcodeGuardArgConc: return "MVMDispOpcodeGuardArgConc";
        case MVMDispOpcodeGuardArgTypeObject: return "MVMDispOpcodeGuardArgTypeObject";
        case MVMDispOpcodeGuardArgLiteralObj: return "MVMDispOpcodeGuardArgLiteralObj";
        case MVMDispOpcodeGuardArgLiteralStr: return "MVMDispOpcodeGuardArgLiteralStr";
        case MVMDispOpcodeGuardArgLiteralInt: return "MVMDispOpcodeGuardArgLiteralInt";
        case MVMDispOpcodeGuardArgLiteralNum: return "MVMDispOpcodeGuardArgLiteralNum";
        case MVMDispOpcodeGuardArgNotLiteralObj: return "MVMDispOpcodeGuardArgNotLiteralObj";
        case MVMDispOpcodeGuardTempType: return "MVMDispOpcodeGuardTempType";
        case MVMDispOpcodeGuardTempTypeConc: return "MVMDispOpcodeGuardTempTypeConc";
        case MVMDispOpcodeGuardTempTypeTypeObject: return "MVMDispOpcodeGuardTempTypeTypeObject";
        case MVMDispOpcodeGuardTempConc: return "MVMDispOpcodeGuardTempConc";
        case MVMDispOpcodeGuardTempTypeObject: return "MVMDispOpcodeGuardTempTypeObject";
        case MVMDispOpcodeGuardTempLiteralObj: return "MVMDispOpcodeGuardTempLiteralObj";
        case MVMDispOpcodeGuardTempLiteralStr: return "MVMDispOpcodeGuardTempLiteralStr";
        case MVMDispOpcodeGuardTempLiteralInt: return "MVMDispOpcodeGuardTempLiteralInt";
        case MVMDispOpcodeGuardTempLiteralNum: return "MVMDispOpcodeGuardTempLiteralNum";
        case MVMDispOpcodeGuardTempNotLiteralObj: return "MVMDispOpcodeGuardTempNotLiteralObj";
        case MVMDispOpcodeLoadCaptureValue: return "MVMDispOpcodeLoadCaptureValue";
        case MVMDispOpcodeLoadResumeInitValue: return "MVMDispOpcodeLoadResumeInitValue";
        case MVMDispOpcodeLoadResumeState: return "MVMDispOpcodeLoadResumeState";
        case MVMDispOpcodeLoadConstantObjOrStr: return "MVMDispOpcodeLoadConstantObjOrStr";
        case MVMDispOpcodeLoadConstantInt: return "MVMDispOpcodeLoadConstantInt";
        case MVMDispOpcodeLoadConstantNum: return "MVMDispOpcodeLoadConstantNum";
        case MVMDispOpcodeLoadAttributeObj: return "MVMDispOpcodeLoadAttributeObj";
        case MVMDispOpcodeLoadAttributeInt: return "MVMDispOpcodeLoadAttributeInt";
        case MVMDispOpcodeLoadAttributeNum: return "MVMDispOpcodeLoadAttributeNum";
        case MVMDispOpcodeLoadAttributeStr: return "MVMDispOpcodeLoadAttributeStr";
        case MVMDispOpcodeUnboxInt: return "MVMDispOpcodeUnboxInt";
        case MVMDispOpcodeUnboxNum: return "MVMDispOpcodeUnboxNum";
        case MVMDispOpcodeUnboxStr: return "MVMDispOpcodeUnboxStr";
        case MVMDispOpcodeLoadHOW: return "MVMDispOpcodeLoadHOW";
        case MVMDispOpcodeLookup: return "MVMDispOpcodeLookup";
        case MVMDispOpcodeSet: return "MVMDispOpcodeSet";
        case MVMDispOpcodeResultValueObj: return "MVMDispOpcodeResultValueObj";
        case MVMDispOpcodeResultValueStr: return "MVMDispOpcodeResultValueStr";
        case MVMDispOpcodeResultValueInt: return "MVMDispOpcodeResultValueInt";
        case MVMDispOpcodeResultValueNum: return "MVMDispOpcodeResultValueNum";
        case MVMDispOpcodeBindFailureToResumption: return "MVMDispOpcodeBindFailureToResumption";
        case MVMDispOpcodeBindCompletionToResumption: return "MVMDispOpcodeBindCompletionToResumption";
        case MVMDispOpcodeUseArgsTail: return "MVMDispOpcodeUseArgsTail";
        case MVMDispOpcodeCopyArgsTail: return "MVMDispOpcodeCopyArgsTail";
        case MVMDispOpcodeResultBytecode: return "MVMDispOpcodeResultBytecode";
        case MVMDispOpcodeResultCFunction: return "MVMDispOpcodeResultCFunction";
        case MVMDispOpcodeResultForeignCode: return "MVMDispOpcodeResultForeignCode";
        default:
           return "<unknown>";
    }
}
