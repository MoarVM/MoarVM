#include "moar.h"

/* Wraps a C function into a BOOTCCode object. */
static MVMObject * wrap(MVMThreadContext *tc, void (*func) (MVMThreadContext *, MVMArgs)) {
    MVMObject *BOOTCCode = tc->instance->boot_types.BOOTCCode;
    MVMObject *code_obj = REPR(BOOTCCode)->allocate(tc, STABLE(BOOTCCode));
    ((MVMCFunction *)code_obj)->body.func = func;
    return code_obj;
}

/* The boot-constant dispatcher takes the first position argument of the
 * incoming argument catpure and treats it as a constant that should always
 * be produced as the result of the dispatch (modulo established guards). */
static void boot_constant(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMArgProcContext arg_ctx;
    MVM_args_proc_setup(tc, &arg_ctx, arg_info);
    MVM_args_checkarity(tc, &arg_ctx, 1, 1);
    MVMObject *capture = MVM_args_get_required_pos_obj(tc, &arg_ctx, 0);
    MVM_disp_program_record_result_constant(tc, MVM_capture_arg_pos_o(tc, capture, 0));
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
    MVM_disp_program_record_result_capture_value(tc, capture, 0);
    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot value dispatcher. */
MVMObject * MVM_disp_boot_value_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_value);
}

/* The boot-code dispatcher takes the first positional argument of the
 * incoming argument catpure, which should be either an MVMCode or an
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
            MVM_disp_program_record_code_constant(tc, code, args_capture);
        }
        else if (REPR(code)->ID == MVM_REPR_ID_MVMCFunction && IS_CONCRETE(code)) {
            MVM_panic(1, "invoke c function result nyi");
        }
        else {
            MVM_exception_throw_adhoc(tc,
                    "boot-code-constant dispatcher only works with MVMCode or MVMCFunction");
        }
    });

    MVM_args_set_result_obj(tc, tc->instance->VMNull, MVM_RETURN_CURRENT_FRAME);
}

/* Gets the MVMCFunction object wrapping the boot code dispatcher. */
MVMObject * MVM_disp_boot_code_constant_dispatch(MVMThreadContext *tc) {
    return wrap(tc, boot_code_constant);
}
