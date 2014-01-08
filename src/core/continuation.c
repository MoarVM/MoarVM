#include "moar.h"

static MVMCallsite      no_arg_callsite = { NULL, 0, 0, 0 };
static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     obj_arg_callsite = { obj_arg_flags, 1, 1, 0 };

static void clear_tag(MVMThreadContext *tc, void *sr_data) {
    MVMContinuationTag **update = &tc->cur_frame->continuation_tags;
    while (*update) {
        if (*update == sr_data) {
            *update = (*update)->next;
            free(sr_data);
            return;
        }
        update = &((*update)->next);
    }
    MVM_exception_throw_adhoc(tc, "Internal error: failed to clear continuation tag");
}
void MVM_continuation_reset(MVMThreadContext *tc, MVMObject *tag, 
                            MVMObject *code, MVMRegister *res_reg) {
    /* Save the tag. */
    MVMContinuationTag *tag_record = malloc(sizeof(MVMContinuationTag));
    tag_record->tag = tag;
    tag_record->next = tc->cur_frame->continuation_tags;
    tc->cur_frame->continuation_tags = tag_record;

    /* Run the passed code. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, &no_arg_callsite);
    tc->cur_frame->special_return = clear_tag;
    tc->cur_frame->special_return_data = tag_record;
    STABLE(code)->invoke(tc, code, &no_arg_callsite, tc->cur_frame->args);
}

void MVM_continuation_control(MVMThreadContext *tc, MVMint64 protect,
                              MVMObject *tag, MVMObject *code,
                              MVMRegister *res_reg) {
    MVMObject *cont;

    /* Hunt the tag on the stack; mark frames as being incorporated into a
     * continuation as we go to avoid a second pass. */
    MVMFrame           *jump_frame  = tc->cur_frame;
    MVMFrame           *root_frame  = NULL;
    MVMContinuationTag *tag_record  = NULL;
    while (jump_frame) {
        jump_frame->in_continuation = 1;
        tag_record = jump_frame->continuation_tags;
        while (tag_record) {
            if (!tag || tag_record->tag == tag)
                break;
            tag_record = tag_record->next;
        }
        if (tag_record)
            break;
        root_frame = jump_frame;
        jump_frame = jump_frame->caller;
    }
    if (!tag_record)
        MVM_exception_throw_adhoc(tc, "No matching continuation reset found");
    if (!root_frame)
        MVM_exception_throw_adhoc(tc, "No continuation root frame found");

    /* Create continuation. */
    MVMROOT(tc, code, {
        cont = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContinuation);
        ((MVMContinuation *)cont)->body.top     = MVM_frame_inc_ref(tc, tc->cur_frame);
        ((MVMContinuation *)cont)->body.addr    = *tc->interp_cur_op;
        ((MVMContinuation *)cont)->body.res_reg = res_reg;
        ((MVMContinuation *)cont)->body.root    = MVM_frame_inc_ref(tc, root_frame);
    });

    /* Move back to the frame with the reset in it. */
    MVM_frame_dec_ref(tc, tc->cur_frame);
    tc->cur_frame = MVM_frame_inc_ref(tc, jump_frame);
    *(tc->interp_cur_op) = tc->cur_frame->return_address;
    *(tc->interp_bytecode_start) = tc->cur_frame->static_info->body.bytecode;
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Clear special return handler, given we didn't just fall out of the
     * reset. */
    tc->cur_frame->special_return = NULL;
    tc->cur_frame->special_return_data = NULL;

    /* If we're not protecting the follow-up call, remove the tag record. */
    if (!protect)
        clear_tag(tc, tag_record);

    /* Invoke specified code, passing the continuation. We return to
     * interpreter to run this, which then returns control to the
     * original reset or invoke. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, tc->cur_frame->return_value, tc->cur_frame->return_type, &obj_arg_callsite);
    tc->cur_frame->args[0].o = cont;
    STABLE(code)->invoke(tc, code, &obj_arg_callsite, tc->cur_frame->args);
}

void MVM_continuation_invoke(MVMThreadContext *tc, MVMContinuation *cont,
                             MVMObject *code, MVMRegister *res_reg) {
    /* Switch caller of the root to current invoker. */
    /* XXX Probably not quite right yet. */
    MVMFrame *orig_caller = cont->body.root->caller;
    cont->body.root->caller = MVM_frame_inc_ref(tc, tc->cur_frame);
    MVM_frame_dec_ref(tc, orig_caller);

    /* Set up current frame to receive result. */
    tc->cur_frame->return_value = res_reg;
    tc->cur_frame->return_type = MVM_RETURN_OBJ;
    tc->cur_frame->return_address = *(tc->interp_cur_op);

    /* Switch to the target frame. */
    MVM_frame_dec_ref(tc, tc->cur_frame);
    tc->cur_frame = MVM_frame_inc_ref(tc, cont->body.top);
    *(tc->interp_cur_op) = cont->body.addr;
    *(tc->interp_bytecode_start) = tc->cur_frame->static_info->body.bytecode;
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Invoke the specified code, putting its result in the specified result
     * register. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    MVM_args_setup_thunk(tc, cont->body.res_reg, MVM_RETURN_OBJ, &no_arg_callsite);
    STABLE(code)->invoke(tc, code, &no_arg_callsite, tc->cur_frame->args);
}
