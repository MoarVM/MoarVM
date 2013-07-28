#include "moarvm.h"

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
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
        MVMuint32 old_size = tc->frame_pool_table_size;
        MVMuint32 new_size = tc->frame_pool_table_size;
        do {
            new_size *= 2;
        } while (static_frame->pool_index >= new_size);

        tc->frame_pool_table = realloc(tc->frame_pool_table,
            new_size * sizeof(MVMFrame *));
        memset(tc->frame_pool_table + old_size, 0,
            (new_size - old_size) * sizeof(MVMFrame *));
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
    while (apr_atomic_dec32(&frame->ref_count) == 0) {
        MVMuint32 pool_index = frame->static_info->pool_index;
        MVMFrame *node = tc->frame_pool_table[pool_index];
        MVMFrame *outer_to_decr = frame->outer;

        if (node && node->ref_count >= MVMFramePoolLengthLimit) {
            /* There's no room on the free list, so destruction.*/
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
        if (outer_to_decr)
            frame = outer_to_decr; /* and loop */
        else
            return;
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
        frame->params.named_used = NULL;

        /* Copy thread context into the frame. */
        frame->tc = tc;

        /* Set static frame. */
        frame->static_info = static_frame;

        /* Ensure special return pointer is null. */
        frame->special_return = NULL;
    }
    else {
        tc->frame_pool_table[pool_index] = node->outer;
        frame = node;
    }

    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;

    /* Allocate space for lexicals and work area, copying the default lexical
     * environment into place. */
    if (static_frame->env_size) {
        if (fresh)
            frame->env = malloc(static_frame->env_size);
        memcpy(frame->env, static_frame->static_env, static_frame->env_size);
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
        frame->outer = NULL;
        while (candidate) {
            if (candidate->static_info == static_frame->outer) {
                frame->outer = candidate;
                break;
            }
            candidate = candidate->caller;
        }
        if (!frame->outer)
            frame->outer = static_frame->outer->prior_invocation;
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

/* Creates a frame that is suitable for deserializing a context into. Does not
 * try to use the frame pool, since we'll typically never recycle these. */
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref) {
    MVMFrame *frame;

    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame->invoked)
        prepare_and_verify_static_frame(tc, static_frame);

    frame = malloc(sizeof(MVMFrame));
    memset(frame, 0, sizeof(MVMFrame));

    /* Copy thread context into the frame. */
    frame->tc = tc;

    /* Set static frame. */
    frame->static_info = static_frame;

    /* Store the code ref. */
    frame->code_ref = code_ref;

    /* Allocate space for lexicals, copying the default lexical environment
     * into place. */
    if (static_frame->env_size) {
        frame->env = malloc(static_frame->env_size);
        memcpy(frame->env, static_frame->static_env, static_frame->env_size);
    }

    /* Initial reference count is 0 (will become referenced by being set as
     * an outer context). So just return it now. */
    return frame;
}

/* Return/unwind do about the same thing; this factors it out. */
static MVMuint64 return_or_unwind(MVMThreadContext *tc, MVMuint8 unwind) {
    MVMFrame *returner = tc->cur_frame;
    MVMFrame *caller = returner->caller;
    MVMFrame *prior;

    /* Decrement the frame reference of the prior invocation, and then
     * set us as it. */
    do {
        prior = returner->static_info->prior_invocation;
    } while (!MVM_cas(&returner->static_info->prior_invocation, prior, returner));
    if (prior)
        MVM_frame_dec_ref(tc, prior);

    /* Clear up argument processing leftovers, if any. */
    if (returner->work) {
        MVM_args_proc_cleanup_for_cache(tc, &returner->params);
    }

    /* signal to the GC to ignore ->work */
    returner->tc = NULL;

    /* Switch back to the caller frame if there is one; we also need to
     * decrement its reference count. */
    if (caller && returner != tc->thread_entry_frame) {
        tc->cur_frame = caller;
        *(tc->interp_cur_op) = caller->return_address;
        *(tc->interp_bytecode_start) = caller->static_info->bytecode;
        *(tc->interp_reg_base) = caller->work;
        *(tc->interp_cu) = caller->static_info->cu;
        MVM_frame_dec_ref(tc, caller);
        returner->caller = NULL;

        /* Handle any special return hooks. */
        if (caller->special_return) {
            MVMSpecialReturn sr = caller->special_return;
            caller->special_return = NULL;
            if (!unwind)
                sr(tc, caller->special_return_data);
        }

        return 1;
    }
    else {
        tc->cur_frame = NULL;
        return 0;
    }
}

/* Attempt to return from the current frame. Returns non-zero if we can,
 * and zero if there is nowhere to return to (which would signal the exit
 * of the interpreter). */
MVMuint64 MVM_frame_try_return(MVMThreadContext *tc) {
    return return_or_unwind(tc, 0);
}

/* Attempt to unwind the current frame. Returns non-zero if we can, and
 * zero if we've nowhere to unwind to (which signifies we're hit the exit
 * point of the interpreter - which probably shouldn't happen, but caller
 * will be in a better place to give an error). */
MVMuint64 MVM_frame_try_unwind(MVMThreadContext *tc) {
    return return_or_unwind(tc, 1);
}

/* Given the specified code object, copies it and returns a copy which
 * captures a closure over the current scope. */
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *closure;
    MVMStaticFrame *sf;

    if (!code || REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform takeclosure on object with representation MVMCode");

    sf = ((MVMCode *)code)->body.sf;
    closure = (MVMCode *)REPR(code)->allocate(tc, STABLE(code));

    closure->body.sf    = sf;
    closure->body.outer = MVM_frame_inc_ref(tc, tc->cur_frame);
    MVM_ASSIGN_REF(tc, closure, closure->body.code_object, ((MVMCode *)code)->body.code_object);

    return (MVMObject *)closure;
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. An error is thrown if it does not exist or if the
 * type is incorrect */
MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalHashEntry *lexical_names = cur_frame->static_info->lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one
             * to avoid semi-predicate issue. */
            MVMLexicalHashEntry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

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

/* Looks up the address of the lexical with the specified name and the
 * specified type. Returns null if it does not exist. */
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type) {
    MVMFrame *cur_frame = tc->cur_frame;
    if (!name) {
        MVM_exception_throw_adhoc(tc, "Contextual name cannot be null");
    }
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalHashEntry *lexical_names = cur_frame->static_info->lexical_names;
        if (lexical_names) {
            MVMLexicalHashEntry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                *type = cur_frame->static_info->lexical_types[entry->value];
                return &cur_frame->env[entry->value];
            }
        }
        cur_frame = cur_frame->caller;
    }
    return NULL;
}

MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type);
    MVMObject *result = NULL, *result_type = NULL;
    if (lex_reg) {
        switch (type) {
            case MVM_reg_int64:
                result_type = (*tc->interp_cu)->hll_config->int_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing int box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs->set_int(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->i64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_num64:
                result_type = (*tc->interp_cu)->hll_config->num_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing num box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs->set_num(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->n64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_str:
                result_type = (*tc->interp_cu)->hll_config->str_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing str box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs->set_str(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->s);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_obj:
                result = lex_reg->o;
                break;
            default:
                MVM_exception_throw_adhoc(tc, "invalid register type in getdynlex");
        }
    }
    return result;
}

void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type);
    if (!lex_reg) {
        MVM_exception_throw_adhoc(tc, "No contextual found with name '%s'",
            MVM_string_utf8_encode_C_string(tc, name));
    }
    switch (type) {
        case MVM_reg_int64:
            lex_reg->i64 = REPR(value)->box_funcs->get_int(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_num64:
            lex_reg->n64 = REPR(value)->box_funcs->get_num(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_str:
            lex_reg->s = REPR(value)->box_funcs->get_str(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_obj:
            lex_reg->o = value;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "invalid register type in binddynlex");
    }
}

/* Returns the storage unit for the lexical in the specified frame. */
MVMRegister * MVM_frame_lexical(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    MVMLexicalHashEntry *lexical_names = f->static_info->lexical_names;
    if (lexical_names) {
        MVMLexicalHashEntry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry)
            return &f->env[entry->value];
    }
    MVM_exception_throw_adhoc(tc, "Frame has no lexical with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

/* Returns the primitive type specification for a lexical. */
MVMuint16 MVM_frame_lexical_primspec(MVMThreadContext *tc, MVMFrame *f, MVMString *name) {
    MVMLexicalHashEntry *lexical_names = f->static_info->lexical_names;
    if (lexical_names) {
        MVMLexicalHashEntry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry) {
            switch (f->static_info->lexical_types[entry->value]) {
                case MVM_reg_int64:
                    return MVM_STORAGE_SPEC_BP_INT;
                case MVM_reg_num64:
                    return MVM_STORAGE_SPEC_BP_NUM;
                case MVM_reg_str:
                    return MVM_STORAGE_SPEC_BP_STR;
                case MVM_reg_obj:
                    return MVM_STORAGE_SPEC_BP_NONE;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "Unhandled lexical type in lexprimspec for '%s'",
                        MVM_string_utf8_encode_C_string(tc, name));
            }
        }
    }
    MVM_exception_throw_adhoc(tc, "Frame has no lexical with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

MVMObject * MVM_frame_find_invokee(MVMThreadContext *tc, MVMObject *code) {
    if (STABLE(code)->invoke == MVM_6model_invoke_default) {
        MVMInvocationSpec *is = STABLE(code)->invocation_spec;
        if (!is) {
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object");
        }
        if (is->class_handle) {
            MVMRegister dest;
            REPR(code)->attr_funcs->get_attribute(tc,
                STABLE(code), code, OBJECT_BODY(code),
                is->class_handle, is->attr_name,
                is->hint, &dest, MVM_reg_obj);
            code = dest.o;
        }
        else {
            code = is->invocation_handler;
        }
    }
    return code;
}
