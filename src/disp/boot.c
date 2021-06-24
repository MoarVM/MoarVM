#include "moar.h"

/* Wraps a C function into a BOOTCCode object. */
static MVMObject * wrap(MVMThreadContext *tc, void (*func) (MVMThreadContext *, MVMArgs)) {
    MVMObject *BOOTCCode = tc->instance->boot_types.BOOTCCode;
    MVMObject *code_obj = REPR(BOOTCCode)->allocate(tc, STABLE(BOOTCCode));
    ((MVMCFunction *)code_obj)->body.func = func;
    return code_obj;
}

/* The boot-constant dispatcher takes the first position argument of the
 * incoming argument capture and treats it as a constant that should always
 * be produced as the result of the dispatch (modulo established guards). */
static void boot_constant(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMRegister value;
    MVMCallsiteFlags kind;
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_capture_arg_pos(tc, capture, 0, &value, &kind);
    MVM_disp_program_record_result_constant(tc, kind, value);
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot constant dispatcher. */
MVMObject * MVM_disp_boot_constant_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_constant);
}

/* The boot-value dispatcher returns the first positional argument of the
 * incoming argument capture. */
static void boot_value(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_result_tracked_value(tc,
            MVM_disp_program_record_track_arg(tc, capture, 0));
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot value dispatcher. */
MVMObject * MVM_disp_boot_value_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_value);
}

/* The boot-code-constant dispatcher takes the first positional argument of
 * the incoming argument capture, which should be either an MVMCode or an
 * MVMCFunction. It invokes it with the rest of the args. The provided
 * code object is considered a constant. */
static void boot_code_constant(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMROOT(tc, capture, {
        /* Get a capture dropping the first argument, which is the callee. */
        MVMObject *args_capture = MVM_disp_program_record_capture_drop_arg(tc, capture, 0);

        /* Work out what the callee is, and set us up to invoke it. */
        MVMObject *code = MVM_capture_arg_pos_o(tc, capture, 0);
        if (REPR(code)->ID == MVM_REPR_ID_MVMCode && IS_CONCRETE(code)) {
            MVM_disp_program_record_code_constant(tc, (MVMCode *)code, args_capture);
        }
        else if (REPR(code)->ID == MVM_REPR_ID_MVMCFunction && IS_CONCRETE(code)) {
            MVM_disp_program_record_c_code_constant(tc, (MVMCFunction *)code, args_capture);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                    "boot-code-constant dispatcher only works with MVMCode or MVMCFunction");
        }
    });

    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot code constant dispatcher. */
MVMObject * MVM_disp_boot_code_constant_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_code_constant);
}

/* The boot-code dispatcher takes the first positional argument of the
 * incoming argument capture, which should be either an MVMCode or an
 * MVMCFunction. It establishes a type and concreteness guard on it,
 * then invokes it with the rest of the args. */
static void boot_code(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVMROOT(tc, capture, {
        /* Get a capture dropping the first argument, which is the callee. */
        MVMObject *args_capture = MVM_disp_program_record_capture_drop_arg(tc, capture, 0);

        /* Work out what the callee is, and set us up to invoke it. */
        MVMObject *code = MVM_capture_arg_pos_o(tc, capture, 0);
        MVMObject *tracked_code = MVM_disp_program_record_track_arg(tc, capture, 0);
        if (REPR(code)->ID == MVM_REPR_ID_MVMCode && IS_CONCRETE(code)) {
            MVM_disp_program_record_tracked_code(tc, tracked_code, args_capture);
        }
        else if (REPR(code)->ID == MVM_REPR_ID_MVMCFunction && IS_CONCRETE(code)) {
            MVM_disp_program_record_tracked_c_code(tc, tracked_code, args_capture);
        }
        else {
            MVM_exception_throw_adhoc(tc,
                    "boot-code dispatcher only works with MVMCode or MVMCFunction");
        }
    });

    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot code dispatcher. */
MVMObject * MVM_disp_boot_code_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_code);
}

/* The boot-syscall dispatcher expects the first argument to be a string,
 * which will typically be a literal. It uses this to invoke functionality
 * provided by the VM. The rest of the arguments are the arguments that go
 * to that call. */
static void boot_syscall(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);

    /* Look up the syscall information. */
    MVMString *name = MVM_capture_arg_pos_s(tc, capture, 0);
    MVMDispSysCall *syscall = MVM_disp_syscall_find(tc, name);
    if (!syscall) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
                "No MoarVM syscall with name '%s'", c_name);
    }

    /* Drop the name from the args capture, and then check them. */
    MVMObject *args_capture = MVM_disp_program_record_capture_drop_arg(tc, capture, 0);
    MVMCallsite *cs = ((MVMCapture *)args_capture)->body.callsite;
    if (MVM_callsite_has_nameds(tc, cs)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
                "Cannot pass named arguments to MoarVM syscall '%s'", c_name);
    }
    if (cs->num_pos < syscall->min_args) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
                "Too few arguments to MoarVM syscall '%s'; got %d, need %d..%d",
                        c_name, cs->num_pos, syscall->min_args, syscall->max_args);
    }
    if (cs->num_pos > syscall->max_args) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
                "Too many arguments to MoarVM syscall '%s'; got %d, need %d..%d",
                        c_name, cs->num_pos, syscall->min_args, syscall->max_args);
    }
    MVMuint32 i;
    for (i = 0; i < cs->num_pos; i++) {
        /* Check we got the expected kind of argument. */
        if ((cs->arg_flags[i] & MVM_CALLSITE_ARG_TYPE_MASK) != syscall->expected_kinds[i]) {
            char *c_name = MVM_string_utf8_encode_C_string(tc, name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                    "Argument %d to MoarVM syscall '%s' had kind %s, but should be %s",
                    i, c_name,
                    MVM_callsite_arg_type_name(cs->arg_flags[i]),
                    MVM_callsite_arg_type_name(syscall->expected_kinds[i]));
        }

        /* Add any guards. */
        if (syscall->expected_kinds[i] == MVM_CALLSITE_ARG_OBJ) {
            if (syscall->expected_reprs[i]) {
                MVMuint32 expected = syscall->expected_reprs[i];
                MVMuint32 got = REPR(MVM_capture_arg_pos_o(tc, args_capture, i))->ID;
                if (expected == got) {
                    MVMROOT2(tc, name, args_capture, {
                        MVM_disp_program_record_guard_type(tc,
                                MVM_disp_program_record_track_arg(tc, args_capture, i));
                    });
                }
                else {
                    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                            "Argument %d to MoarVM syscall '%s' has repr %s, but should be %s",
                            i, c_name,
                            MVM_repr_get_by_id(tc, got)->name,
                            MVM_repr_get_by_id(tc, expected)->name);
                }
            }
            if (syscall->expected_concrete[i]) {
                if (IS_CONCRETE(MVM_capture_arg_pos_o(tc, args_capture, i))) {
                    MVMROOT2(tc, name, args_capture, {
                        MVM_disp_program_record_guard_concreteness(tc,
                                MVM_disp_program_record_track_arg(tc, args_capture, i));
                    });
                }
                else {
                    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
                    char *waste[] = { c_name, NULL };
                    MVM_exception_throw_adhoc_free(tc, waste,
                            "Argument %d to MoarVM syscall '%s' must be concrete, not a type object",
                            i, c_name);
                }
            }
        }
    }

    /* Produce an invoke C function outcome. */
    MVM_disp_program_record_c_code_constant(tc, syscall->wrapper, args_capture);
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot syscall dispatcher. */
MVMObject * MVM_disp_boot_syscall_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_syscall);
}

/* The boot-resume dispatcher resumes the first dispatcher found
 * by walking down the call stack (not counting the current one). */
static void boot_resume(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_resume(tc, capture);
}

/* Gets the MVMCFunction object wrapping the boot resume dispatcher. */
MVMObject * MVM_disp_boot_resume_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_resume);
}

/* The boot-resume-caller dispatcher skips past the current frame on
 * the callstack (and any immediately preceding dispatcher), and then
 * proceeds as `boot-resume` would. */
static void boot_resume_caller(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_resume_caller(tc, capture);
}

/* Gets the MVMCFunction object wrapping the boot resume caller dispatcher. */
MVMObject * MVM_disp_boot_resume_caller_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_resume_caller);
}

/* The lang-call dispatcher first looks at if we have a VM-level code handle
 * or C function handle, and if so invokes it. Otherwise, it looks at the
 * HLL of the type of the object it finds, resolves the dispatcher of that
 * HLL, and then delegates to it. If there is no dispatcher found for the
 * HLL in question, an exception is thrown. It expects the first argument in
 * the capture to be the target of the invocation and the rest to be the
 * arguments. Establishes a type guard on the callee. */
static void lang_call(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);

    /* Obtain and guard on the first argument of the capture, which is the
     * thing to invoke. */
    MVMObject *invokee = MVM_capture_arg_pos_o(tc, capture, 0);
    MVMObject *tracked_invokee;
    MVMROOT(tc, capture, {
         tracked_invokee = MVM_disp_program_record_track_arg(tc, capture, 0);
    });
    MVM_disp_program_record_guard_type(tc, tracked_invokee);

    /* If it's a VM code object or a VM function, we'll delegate to the
     * boot code dispatcher. */
    MVMString *delegate;
    if (REPR(invokee)->ID == MVM_REPR_ID_MVMCode ||
            REPR(invokee)->ID == MVM_REPR_ID_MVMCFunction) {
        if (!IS_CONCRETE(invokee))
            MVM_exception_throw_adhoc(tc, "lang-code code handle must be concrete");
        MVM_disp_program_record_guard_concreteness(tc, tracked_invokee);
        delegate = tc->instance->str_consts.boot_code;
    }

    /* Otherwise go on langauge and delegate to its registered disaptcher. */
    else {
        MVMHLLConfig *hll = STABLE(invokee)->hll_owner;
        if (!hll)
            MVM_exception_throw_adhoc(tc,
                    "lang-call cannot invoke object of type '%s' belonging to no language",
                    STABLE(invokee)->debug_name);
        delegate = hll->call_dispatcher;
        if (!delegate) {
            char *lang_name = MVM_string_utf8_encode_C_string(tc, hll->name);
            char *waste[] = { lang_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                    "No language call dispatcher registered for %s",
                    lang_name);
        }
    }

    MVM_disp_program_record_delegate(tc, delegate, capture);
}

/* Gets the MVMCFunction object wrapping the language-sensitive call dispatcher. */
MVMObject * MVM_disp_lang_call_dispatch(MVMThreadContext *tc) {
    return wrap(tc, lang_call);
}

/* The lang-meth-call dispatcher looks at the language of the type of the
 * invocant, which should be in the first argument of the capture. If
 * there is one, it delegates to the registered method dispatcher for that
 * language, establishing a type guard on the invocant. If not, then the
 * type must have a metaclass that is a KnowHOW, and the method will be
 * resolved using the method table, with guards being established on both
 * the invocant and the name (which must be the second argument). There
 * will then be a delegation to the lang-call dispatcher to take care of
 * the invocation. The arguments (including the invocant, possibly simply
 * repeated or possibly in a container) should follow the method name. If
 * there is neither a language set on the invocant and it does not have a
 * KnowHOW metaclass, an exception will be thrown. */
static void lang_meth_call(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);

    /* Obtain and guard on the first argument of the capture, which is the
     * invocant of the method call. */
    MVMObject *invocant = MVM_capture_arg_pos_o(tc, capture, 0);
    MVMObject *tracked_invocant;
    MVMROOT(tc, capture, {
         tracked_invocant = MVM_disp_program_record_track_arg(tc, capture, 0);
    });
    MVM_disp_program_record_guard_type(tc, tracked_invocant);

    /* If the invocant has an associated HLL and method dispatcher, delegate there. */
    MVMHLLConfig *hll = STABLE(invocant)->hll_owner;
    if (hll && hll->method_call_dispatcher) {
        MVM_disp_program_record_delegate(tc, hll->method_call_dispatcher, capture);
        return;
    }

    /* Otherwise if it's a KnowHOW, then look in its method table (this is how
     * method dispatch bottoms out in the VM). */
    MVMObject *HOW = MVM_6model_get_how(tc, STABLE(invocant));
    if (REPR(HOW)->ID == MVM_REPR_ID_KnowHOWREPR && IS_CONCRETE(HOW)) {
        MVMObject *methods = ((MVMKnowHOWREPR *)HOW)->body.methods;
        MVMString *method_name = MVM_capture_arg_pos_s(tc, capture, 1);
        MVMObject *method = MVM_repr_at_key_o(tc, methods, method_name);
        if (IS_CONCRETE(method)) {
            MVMROOT2(tc, capture, method, {
                /* Method found. Guard on the name. */
                MVMObject *tracked_name = MVM_disp_program_record_track_arg(tc, capture, 1);
                MVM_disp_program_record_guard_literal(tc, tracked_name);

                /* Drop leading invocant and name. */
                MVMObject *args_capture = MVM_disp_program_record_capture_drop_arg(tc,
                        MVM_disp_program_record_capture_drop_arg(tc, capture, 0), 0);

                /* Insert resolved method. */
                MVMRegister method_reg = { .o = method };
                MVMObject *del_capture = MVM_disp_program_record_capture_insert_constant_arg(tc,
                        args_capture, 0, MVM_CALLSITE_ARG_OBJ, method_reg);
                MVM_disp_program_record_delegate(tc, tc->instance->str_consts.lang_call,
                        del_capture);
            });
        }
        else {
            char *c_name = MVM_string_utf8_encode_C_string(tc, method_name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                    "Cannot find method '%s' on object of type %s",
                    c_name, STABLE(invocant)->debug_name);
        }
        return;
    }

    /* Otherwise, error. */
    MVM_exception_throw_adhoc(tc,
            "lang-meth-call cannot work out how to dispatch on type '%s'",
            STABLE(invocant)->debug_name);
}

/* Gets the MVMCFunction object wrapping the language-sensitive method call
 * dispatcher. */
MVMObject * MVM_disp_lang_meth_call_dispatch(MVMThreadContext *tc) {
    return wrap(tc, lang_meth_call);
}
