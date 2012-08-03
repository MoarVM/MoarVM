#include "moarvm.h"

/* Obtains a call frame ready to be filled out. Makes no promises that the
 * frame data structure will be zeroed out. */
static MVMFrame * obtain_frame(MVMThreadContext *tc) {
    /* XXX Don't need to malloc every time; maintain a free list. */
    return malloc(sizeof(MVMFrame));
}

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    /* Calculate lexicals storage needed. */
    static_frame->env_size = static_frame->num_lexicals * sizeof(MVMRegister);
    
    /* Work size is number of locals/registers plus size of the maximum
     * call site argument list. */
    static_frame->work_size = sizeof(MVMRegister) *
        (static_frame->num_locals + static_frame->cu->max_callsite_size);

    /* Validate the bytecode. */
    MVM_validate_static_frame(tc, static_frame);
    
    /* Mark frame as invoked, so we need not do these calculations again. */
    static_frame->invoked = 1;
}

/* Increases the reference count of a frame. */
MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame) {
    apr_atomic_inc32(&frame->ref_count);
    return frame;
}

/* Decreases the reference count of a frame. If it hits zero, then we can
 * free it. */
void MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame) {
    /* Note that we get zero if we really hit zero here, but dec32 may
     * not give the exact count back if it ends up non-zero. */
    if (apr_atomic_dec32(&frame->ref_count) == 0) {
        if (frame->outer)
            MVM_frame_dec_ref(tc, frame->outer);
        if (frame->env) {
            free(frame->env);
            frame->env = NULL;
        }
        if (frame->work) {
            MVM_args_proc_cleanup(tc, &frame->params);
            free(frame->work);
            frame->work = NULL;
        }
        free(frame);
    }
}

/* Takes a static frame and a thread context. Invokes the static frame. */
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref) {
    /* Get a fresh frame data structure. */
    MVMFrame *frame = obtain_frame(tc);
    
    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame->invoked)
        prepare_and_verify_static_frame(tc, static_frame);
    
    /* Copy thread context into the frame. */
    frame->tc = tc;
    
    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;
    
    /* Allocate space for lexicals and work area. */
    /* XXX Do something better than malloc here some day. */
    if (static_frame->env_size) {
        frame->env = malloc(static_frame->env_size);
        memset(frame->env, 0, static_frame->env_size);
    }
    else {
        frame->env = NULL;
    }
    if (static_frame->work_size) {
        frame->work = malloc(static_frame->work_size);
        memset(frame->work, 0, static_frame->work_size);
    }
    else {
        frame->work = NULL;
    }
    
    /* Calculate args buffer position. */
    frame->args = static_frame->work_size ?
        frame->work + static_frame->num_locals :
        NULL;

    /* Outer. */
    if (outer) {
        /* We were provided with an outer frame; just ensure that it is
         * based on the correct static frame. */
        if (outer->static_info == static_frame->outer)
            frame->outer = outer;
        else
            MVM_exception_throw_adhoc(tc,
                "Provided outer frame does not match expected static frame type");
    }
    else if (static_frame->outer) {
        /* We need an outer, but none was provided by a closure. See if
         * we can find an appropriate frame on the current call stack. */
        MVMFrame *candidate = tc->cur_frame;
        while (candidate) {
            if (candidate->static_info == static_frame->outer) {
                frame->outer = candidate;
                break;
            }
            candidate = candidate->caller;
        }
        if (!frame->outer)
            MVM_exception_throw_adhoc(tc,
                "Cannot locate an outer frame for the call");
    }
    else {
        frame->outer = NULL;
    }
    if (frame->outer)
        MVM_frame_inc_ref(tc, frame->outer);
    
    /* Caller is current frame in the thread context. */
    if (tc->cur_frame)
        frame->caller = MVM_frame_inc_ref(tc, tc->cur_frame);
    else
        frame->caller = NULL;

    /* Set static frame. */
    frame->static_info = static_frame;
    
    /* Initial reference count is 1 by virtue of it being the currently
     * executing frame. */
    frame->ref_count = 1;
    
    /* Initialize argument processing. */
    MVM_args_proc_init(tc, &frame->params, callsite, args);
    
    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    *(tc->interp_cur_op) = static_frame->bytecode;
    *(tc->interp_bytecode_start) = static_frame->bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame->cu;
}

/* Attempt to return from the current frame. Returns non-zero if we can,
 * and zero if there is nowhere to return to (which would signal the exit
 * of the interpreter). */
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc) {
    /* Clear up the work area, which is not needed beyond the return.
     * (The lexical environment is left in place, though). */
    MVMFrame *returner = tc->cur_frame;
    MVMFrame *caller = returner->caller; 
    if (returner->work) {
        MVM_args_proc_cleanup(tc, &returner->params);
        free(returner->work);
        returner->work = NULL;
    }

    /* Decrement the frame reference (which, if it is not referenced by
     * anything else, may free it overall). */
    MVM_frame_dec_ref(tc, returner);
    
    /* Switch back to the caller frame if there is one; we also need to
     * decrement its reference count. */
    if (caller) {
        tc->cur_frame = caller;
        *(tc->interp_cur_op) = caller->return_address;
        *(tc->interp_bytecode_start) = caller->static_info->bytecode;
        *(tc->interp_reg_base) = caller->work;
        *(tc->interp_cu) = caller->static_info->cu;
        MVM_frame_dec_ref(tc, caller);
        return 1;
    }
    else {
        return 0;
    }
}

/* Given the specified code object, copies it and returns a copy which
 * captures a closure over the current scope. */
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *closure;
    
    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform takeclosure on object with representation MVMCode");

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&code);
    closure = (MVMCode *)REPR(code)->allocate(tc, STABLE(code));
    MVM_gc_root_temp_pop(tc);
    
    closure->body.sf    = ((MVMCode *)code)->body.sf;
    closure->body.outer = MVM_frame_inc_ref(tc, tc->cur_frame);
    
    return (MVMObject *)closure;
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. An error is thrown if it does not exist or if the
 * type is incorrect */
MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type) {
    MVMFrame *cur_frame = tc->cur_frame;
    while (cur_frame != NULL) {
        apr_hash_t *lexical_names = cur_frame->static_info->lexical_names;
        if (lexical_names) {
            /* Indexes are stored off-by-one to avoid semi-predicate
             * issue. */
            MVMuint16 idx = (MVMuint16)apr_hash_get(lexical_names,
                name->body.data, name->body.graphs * sizeof(MVMint32));
            if (idx) {
                idx--;
                if (cur_frame->static_info->lexical_types[idx] == type)
                    return &cur_frame->env[idx];
                else
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name 'XXX' has wrong type"); /* XXX TODO */ 
            }
        }
        cur_frame = cur_frame->outer;
    }
    MVM_exception_throw_adhoc(tc,
        "No lexical found with name 'XXX'"); /* XXX TODO */
}
