#include "moar.h"

MVMObject * MVM_coroutine_create(MVMThreadContext *tc, MVMObject *code) {
    MVMObject * coroutine = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCoroutine);

    ((MVMCoroutine *)coroutine)->body.code = code;
    tc->cur_frame->coroutine = coroutine;

    return coroutine;
}

void MVM_coroutine_yield(MVMThreadContext *tc, MVMObject *args) {

    ((MVMCoroutine *)tc->cur_frame->coroutine)->body.addr = *(tc->interp_cur_op);

    if(!args) {
        MVM_args_set_result_obj(tc, args, MVM_RETURN_CALLER_FRAME);
    }
    else {
        MVM_args_set_result_obj(tc, tc->cur_frame->args[0].o, MVM_RETURN_CALLER_FRAME);
    }

    MVM_frame_try_return_no_exit_handlers(tc);
}

void MVM_coroutine_resume(MVMThreadContext *tc, MVMObject *coroutine, MVMObject *args, MVMRegister *res_reg) {
    /* Switch to the target frame. */
    tc->cur_frame = ((MVMCoroutine *)coroutine)->body.top;

    *(tc->interp_cur_op) = ((MVMCoroutine *)coroutine)->body.addr;

    *(tc->interp_bytecode_start) = tc->cur_frame->effective_bytecode;
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;
    
    {
        MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS);
        MVMObject * code = MVM_frame_find_invokee(tc, ((MVMCoroutine *)coroutine)->body.code, NULL);
        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, inv_arg_callsite);
        tc->cur_frame->args[0].o = args;
        STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
    }
}
