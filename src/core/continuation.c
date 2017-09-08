#include "moar.h"

static void clear_tag(MVMThreadContext *tc, void *sr_data) {
    MVMContinuationTag **update = &tc->cur_frame->extra->continuation_tags;
    while (*update) {
        if (*update == sr_data) {
            *update = (*update)->next;
            MVM_free(sr_data);
            return;
        }
        update = &((*update)->next);
    }
    MVM_exception_throw_adhoc(tc, "Internal error: failed to clear continuation tag");
}
void MVM_continuation_reset(MVMThreadContext *tc, MVMObject *tag,
                            MVMObject *code, MVMRegister *res_reg) {
    /* Save the tag. */
    MVMFrameExtra *e = MVM_frame_extra(tc, tc->cur_frame);
    MVMContinuationTag *tag_record = MVM_malloc(sizeof(MVMContinuationTag));
    tag_record->tag = tag;
    tag_record->active_handlers = tc->active_handlers;
    tag_record->next = e->continuation_tags;
    e->continuation_tags = tag_record;

    /* Were we passed code or a continuation? */
    if (REPR(code)->ID == MVM_REPR_ID_MVMContinuation) {
        /* Continuation; invoke it. */
        MVM_continuation_invoke(tc, (MVMContinuation *)code, NULL, res_reg);
    }
    else {
        /* Run the passed code. */
        MVMCallsite *null_args_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS);
        code = MVM_frame_find_invokee(tc, code, NULL);
        MVM_args_setup_thunk(tc, res_reg, MVM_RETURN_OBJ, null_args_callsite);
        MVM_frame_special_return(tc, tc->cur_frame, clear_tag, NULL, tag_record, NULL);
        STABLE(code)->invoke(tc, code, null_args_callsite, tc->cur_frame->args);
    }
}

void MVM_continuation_control(MVMThreadContext *tc, MVMint64 protect,
                              MVMObject *tag, MVMObject *code,
                              MVMRegister *res_reg) {
    MVMObject *cont;
    MVMCallsite *inv_arg_callsite;

    /* Hunt the tag on the stack. Also toss any dynamic variable cache
     * entries, as they may be invalid once the continuation is invoked (the
     * Perl 6 $*THREAD is a good example of a problematic one). */
    MVMFrame           *root_frame  = NULL;
    MVMContinuationTag *tag_record  = NULL;
    MVMFrame            *jump_frame;
    MVMROOT(tc, tag, {
    MVMROOT(tc, code, {
        jump_frame = MVM_frame_force_to_heap(tc, tc->cur_frame);
    });
    });
    while (jump_frame) {
        MVMFrameExtra *e = jump_frame->extra;
        if (e) {
            e->dynlex_cache_name = NULL;
            tag_record = e->continuation_tags;
            while (tag_record) {
                if (MVM_is_null(tc, tag) || tag_record->tag == tag)
                    break;
                tag_record = tag_record->next;
            }
            if (tag_record)
                break;
        }
        root_frame = jump_frame;
        jump_frame = jump_frame->caller;
    }
    if (!tag_record)
        MVM_exception_throw_adhoc(tc, "No matching continuation reset found");
    if (!root_frame)
        MVM_exception_throw_adhoc(tc, "No continuation root frame found");

    /* Create continuation. */
    MVMROOT(tc, code, {
    MVMROOT(tc, jump_frame, {
    MVMROOT(tc, root_frame, {
        cont = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContinuation);
        MVM_ASSIGN_REF(tc, &(cont->header), ((MVMContinuation *)cont)->body.top,
            tc->cur_frame);
        MVM_ASSIGN_REF(tc, &(cont->header), ((MVMContinuation *)cont)->body.root,
            root_frame);
        ((MVMContinuation *)cont)->body.addr    = *tc->interp_cur_op;
        ((MVMContinuation *)cont)->body.res_reg = res_reg;
        if (tc->instance->profiling)
            ((MVMContinuation *)cont)->body.prof_cont =
                MVM_profile_log_continuation_control(tc, root_frame);
    });
    });
    });

    /* Save and clear any active exception handler(s) added since reset. */
    if (tc->active_handlers != tag_record->active_handlers) {
        MVMActiveHandler *ah = tc->active_handlers;
        while (ah) {
            if (ah->next_handler == tag_record->active_handlers) {
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
    tc->cur_frame = jump_frame;
    tc->current_frame_nr = jump_frame->sequence_nr;

    *(tc->interp_cur_op) = tc->cur_frame->return_address;
    *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(tc->cur_frame);
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Clear special return handler, given we didn't just fall out of the
     * reset. */
    MVM_frame_clear_special_return(tc, tc->cur_frame);

    /* If we're not protecting the follow-up call, remove the tag record. */
    if (!protect)
        clear_tag(tc, tag_record);

    /* Invoke specified code, passing the continuation. We return to
     * interpreter to run this, which then returns control to the
     * original reset or invoke. */
    code = MVM_frame_find_invokee(tc, code, NULL);
    inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    MVM_args_setup_thunk(tc, tc->cur_frame->return_value, tc->cur_frame->return_type, inv_arg_callsite);
    tc->cur_frame->args[0].o = cont;
    STABLE(code)->invoke(tc, code, inv_arg_callsite, tc->cur_frame->args);
}

void MVM_continuation_invoke(MVMThreadContext *tc, MVMContinuation *cont,
                             MVMObject *code, MVMRegister *res_reg) {
    /* Ensure we are the only invoker of the continuation. */
    if (!MVM_trycas(&(cont->body.invoked), 0, 1))
        MVM_exception_throw_adhoc(tc, "This continuation has already been invoked");

    /* Switch caller of the root to current invoker. */
    MVMROOT(tc, cont, {
    MVMROOT(tc, code, {
        MVM_frame_force_to_heap(tc, tc->cur_frame);
    });
    });
    MVM_ASSIGN_REF(tc, &(cont->body.root->header), cont->body.root->caller, tc->cur_frame);

    /* Set up current frame to receive result. */
    tc->cur_frame->return_value = res_reg;
    tc->cur_frame->return_type = MVM_RETURN_OBJ;
    tc->cur_frame->return_address = *(tc->interp_cur_op);

    /* Switch to the target frame. */
    tc->cur_frame = cont->body.top;
    tc->current_frame_nr = cont->body.top->sequence_nr;

    *(tc->interp_cur_op) = cont->body.addr;
    *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(tc->cur_frame);
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Put saved active handlers list in place. */
    /* TODO: if we really need to support double-shot, this needs a re-visit.
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
}

void MVM_continuation_free_tags(MVMThreadContext *tc, MVMFrame *f) {
    MVMContinuationTag *tag = f->extra->continuation_tags;
    while (tag) {
        MVMContinuationTag *next = tag->next;
        MVM_free(tag);
        tag = next;
    }
    f->extra->continuation_tags = NULL;
}
