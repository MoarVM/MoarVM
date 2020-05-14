#include "moar.h"

void MVM_continuation_reset(MVMThreadContext *tc, MVMObject *tag,
                            MVMObject *code, MVMRegister *res_reg) {
    /* Continuations always have their base in a stack region, so we can easily
     * slice it off at the continuation control point. There are three cases we
     * might have here.
     * 1. A reset with code. In this case, we will make a new stack region and
     *    put the tag in it. Common.
     * 2. A reset with a continuation that is unprotected. In this case, we do
     *    not need to make a new segment, we'll just steal the tag slot in the
     *    continuation we're invoking. Also common.
     * 3. A reset with a continuation that is protected. Presumably unusual.
     *    We'll make a new stack region with our tag in it. */
    /* Were we passed code or a continuation? */
    if (REPR(code)->ID == MVM_REPR_ID_MVMContinuation) {
        /* Continuation; invoke it. */
        MVMContinuation *cont = (MVMContinuation *)code;
        if (cont->body.protected_tag) {
            MVM_callstack_new_continuation_region(tc, tag);
            MVM_continuation_invoke(tc, (MVMContinuation *)code, NULL, res_reg, NULL);
        }
        else {
            MVM_continuation_invoke(tc, (MVMContinuation *)code, NULL, res_reg, tag);
        }
    }
    else {
        /* Run the passed code. */
        MVM_callstack_new_continuation_region(tc, tag);
        MVMCallsite *null_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS);
        code = MVM_frame_find_invokee(tc, code, NULL);
        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, null_args_callsite);
        STABLE(code)->invoke(tc, code, null_args_callsite, tc->cur_frame->args);
    }

    MVM_CHECK_CALLER_CHAIN(tc, tc->cur_frame);
}

void MVM_continuation_control(MVMThreadContext *tc, MVMint64 protect,
                              MVMObject *tag, MVMObject *code,
                              MVMRegister *res_reg) {
    MVM_jit_code_trampoline(tc);

    /* Allocate the continuation (done here so that we don't have any more
     * allocation while we're slicing the stack frames off). */
    MVMObject *cont;
    MVMROOT2(tc, tag, code, {
        cont = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContinuation);
    });

    /* Find the tag and slice the required regions off the callstack. */
    MVMActiveHandler *active_handler_at_reset;
    MVMCallStackRecord *orig_top = tc->stack_top;
    MVMCallStackRegion *taken_region = MVM_callstack_continuation_slice(tc, tag,
            &active_handler_at_reset);
    if (!taken_region)
        MVM_exception_throw_adhoc(tc, "No matching continuation reset found");

    /* Clear the caller of the first frame in the taken region. */
    MVM_callstack_first_frame_in_region(tc, taken_region)->caller = NULL;

    /* Set up the continuation. */
    ((MVMContinuation *)cont)->body.stack_top = orig_top;
    ((MVMContinuation *)cont)->body.first_region = taken_region;
    ((MVMContinuation *)cont)->body.addr    = *tc->interp_cur_op;
    ((MVMContinuation *)cont)->body.res_reg = res_reg;
    if (tc->instance->profiling) {
        MVM_panic(1, "Need to update profiling continuation support");
//        ((MVMContinuation *)cont)->body.prof_cont =
//            MVM_profile_log_continuation_control(tc, root_frame);
    }

    /* Save and clear any active exception handler(s) added since reset. */
    if (tc->active_handlers != active_handler_at_reset) {
        MVMActiveHandler *ah = tc->active_handlers;
        while (ah) {
            if (ah->next_handler == active_handler_at_reset) {
                /* Found the handler at the point of reset. Slice off the more
                * recent ones. */
                ((MVMContinuation *)cont)->body.active_handlers = tc->active_handlers;
                tc->active_handlers = ah->next_handler;
                ah->next_handler = NULL;
                break;
            }
            ah = ah->next_handler;
        }
    }

    /* Move back to the frame with the reset in it. */
    tc->cur_frame = MVM_callstack_current_frame(tc);
    tc->current_frame_nr = tc->cur_frame->sequence_nr;
    *(tc->interp_cur_op) = tc->cur_frame->return_address;
    *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(tc->cur_frame);
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* If we're protecting the tag, we need to re-instate it again, as reset
     * would, and also remember it in the continuation. */
    if (protect) {
        MVM_callstack_new_continuation_region(tc, tag);
        MVM_ASSIGN_REF(tc, &(cont->header), ((MVMContinuation *)cont)->body.protected_tag,
            tag);
    }

    /* Invoke specified code, passing the continuation. We return to
     * interpreter to run this, which then returns control to the
     * original reset or invoke. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVMCallsite *inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    MVM_args_setup_thunk(tc, tc->cur_frame->return_value, tc->cur_frame->return_type, inv_arg_callsite);
    tc->cur_frame->args[0].o = cont;
    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);

    MVM_CHECK_CALLER_CHAIN(tc, tc->cur_frame);
}

void MVM_continuation_invoke(MVMThreadContext *tc, MVMContinuation *cont,
                             MVMObject *code, MVMRegister *res_reg,
                             MVMObject *insert_tag) {
    /* First of all do a repr id check */
    if (REPR(cont)->ID != MVM_REPR_ID_MVMContinuation)
        MVM_exception_throw_adhoc(tc, "continuationinvoke expects an MVMContinuation");

    /* Ensure we are the only invoker of the continuation. */
    if (!MVM_trycas(&(cont->body.invoked), 0, 1))
        MVM_exception_throw_adhoc(tc, "This continuation has already been invoked");

    /* Walk the frames we are going to put atop of the stack, and see if any
     * are heap frames. Also clear their dynamic tag. */
    MVMCallStackIterator iter;
    MVMFrame *bottom_frame = NULL;
    MVMuint32 have_heap_frame = 0;
    MVM_callstack_iter_frame_init(tc, &iter, cont->body.stack_top);
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMFrame *cur_frame = MVM_callstack_iter_current_frame(tc, &iter);
        if (cur_frame->extra)
            cur_frame->extra->dynlex_cache_name = NULL;
        if (!MVM_FRAME_IS_ON_CALLSTACK(tc, cur_frame))
            have_heap_frame = 1;
        bottom_frame = cur_frame;
    }
    if (!bottom_frame)
        MVM_exception_throw_adhoc(tc, "Corrupt continuation: failed to find bottom frame");

    /* Force current frames to heap if there are heap frames in the continuation,
     * to maintain the no heap -> stack invariant. */
    if (have_heap_frame) {
        MVMROOT3(tc, cont, code, bottom_frame, {
            MVM_frame_force_to_heap(tc, tc->cur_frame);
        });
    }

    /* Switch caller of the root to current invoker. */
    if (MVM_FRAME_IS_ON_CALLSTACK(tc, tc->cur_frame)) {
        bottom_frame->caller = tc->cur_frame;
    }
    else {
        MVM_ASSIGN_REF(tc, &(bottom_frame->header), bottom_frame->caller, tc->cur_frame);
    }

    /* Splice the call stack region(s) from the continuation onto the top
     * of our callstack, optionally replacing the continuation tag record. */
    MVM_callstack_continuation_append(tc, cont->body.first_region,
            cont->body.stack_top,
            cont->body.protected_tag ? cont->body.protected_tag : insert_tag);

    /* Set up current frame to receive result. */
    tc->cur_frame->return_value = res_reg;
    tc->cur_frame->return_type = MVM_RETURN_OBJ;
    tc->cur_frame->return_address = *(tc->interp_cur_op);

    MVM_jit_code_trampoline(tc);

    /* Sync up current frame to the target frame and the interpreter. */
    tc->cur_frame = MVM_callstack_current_frame(tc);
    tc->current_frame_nr = tc->cur_frame->sequence_nr;
    *(tc->interp_cur_op) = cont->body.addr;
    *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(tc->cur_frame);
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Put saved active handlers list in place. */
    /* If we ever need to support multi-shot, continuations this needs a re-visit.
     * As it is, Rakudo's gather/take only needs single-invoke continuations,
     * so we'll punt on the issue for now. */
    if (cont->body.active_handlers) {
        MVMActiveHandler *ah = cont->body.active_handlers;
        while (ah->next_handler)
            ah = ah->next_handler;
        ah->next_handler = tc->active_handlers;
        tc->active_handlers = cont->body.active_handlers;
        cont->body.active_handlers = NULL;
    }

    /* If we're profiling, get it back in sync. */
    if (cont->body.prof_cont && tc->instance->profiling)
        MVM_profile_log_continuation_invoke(tc, cont->body.prof_cont);

    /* Provided we have it, invoke the specified code, putting its result in
     * the specified result register. Otherwise, put a NULL there. */
    if (MVM_is_null(tc, code)) {
        cont->body.res_reg->o = tc->instance->VMNull;
    }
    else {
        MVMCallsite *null_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS);
        code = MVM_frame_find_invokee(tc, code, NULL);
        MVM_args_setup_thunk(tc, cont->body.res_reg, MVM_RETURN_OBJ, null_args_callsite);
        STABLE(code)->invoke(tc, code, null_args_callsite, tc->cur_frame->args);
    }

    MVM_CHECK_CALLER_CHAIN(tc, tc->cur_frame);
}
