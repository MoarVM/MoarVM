#include "moarvm.h"

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
    
    /* Obtain an index to each threadcontext's pool table */
    static_frame->pool_index = apr_atomic_inc32(&tc->instance->num_frame_pools);
    if (static_frame->pool_index >= tc->frame_pool_table_size) {
        /* Grow the threadcontext's pool table */
        MVMuint32 new_size = tc->frame_pool_table_size;
        do {
            new_size *= 2;
        } while (static_frame->pool_index >= new_size);
        
        tc->frame_pool_table = realloc(tc->frame_pool_table, (size_t)new_size);
        tc->frame_pool_table_size = new_size;
    }
    
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
        MVMuint32 pool_index = frame->static_info->pool_index;
        MVMFrame *node = tc->frame_pool_table[pool_index];
        
        if (node && node->ref_count >= MVMFramePoolLengthLimit) {
            /* There's no room on the free list, so destruction.*/
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
        else { /* Unshift it to the free list */
            frame->ref_count = (frame->outer = node) ? node->ref_count + 1 : 1;
            tc->frame_pool_table[pool_index] = frame;
        }
    }
}

/* Takes a static frame and a thread context. Invokes the static frame. */
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref) {
    MVMFrame *frame;
    
    MVMuint32 pool_index;
    MVMFrame *node;
    int fresh = 0;
    
    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame->invoked)
        prepare_and_verify_static_frame(tc, static_frame);
    
    pool_index = static_frame->pool_index;
    node = tc->frame_pool_table[pool_index];
    
    if (node == NULL) {
        fresh = 1;
        frame = malloc(sizeof(MVMFrame));
        
        /* Copy thread context into the frame. */
        frame->tc = tc;
        
        /* Set static frame. */
        frame->static_info = static_frame;
    }
    else {
        tc->frame_pool_table[pool_index] = node->outer;
        frame = node;
    }
    
    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;
    
    /* Allocate space for lexicals and work area. */
    if (static_frame->env_size) {
        if (fresh)
            frame->env = malloc(static_frame->env_size);
        memset(frame->env, 0, static_frame->env_size);
    }
    else {
        frame->env = NULL;
    }
    if (static_frame->work_size) {
        if (fresh)
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
        
        /* Don't release ->work in case we want the frame to be cached */
        /* free(returner->work);
        returner->work = NULL; */
    }
    /* signal to the GC to ignore ->work */
    returner->tc = NULL;

    /* Decrement the frame reference (which, if it is not referenced by
     * anything else, may free it overall). */
    MVM_frame_dec_ref(tc, returner);
    
    /* Switch back to the caller frame if there is one; we also need to
     * decrement its reference count. */
    if (caller && returner != tc->thread_entry_frame) {
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
        MVMLexicalHashEntry *lexical_names = cur_frame->static_info->lexical_names;
        if (lexical_names) {
            /* Indexes were formerely stored off-by-one
             * to avoid semi-predicate issue. */
            MVMLexicalHashEntry *entry;
            
            HASH_FIND(hash_handle, lexical_names, name->body.data,
                name->body.graphs * sizeof(MVMint32), entry);
            if (entry) {
                if (cur_frame->static_info->lexical_types[entry->value] == type)
                    return &cur_frame->env[entry->value];
                else
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name '%s' has wrong type",
                            MVM_string_utf8_encode_C_string(tc, name));
            }
        }
        cur_frame = cur_frame->outer;
    }
    MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}
