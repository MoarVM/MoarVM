#include "moar.h"

/* Takes a static frame and does various one-off calculations about what
 * space it shall need. Also triggers bytecode verification of the frame's
 * bytecode. */
void prepare_and_verify_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    MVMStaticFrameBody *static_frame_body = &static_frame->body;
    /* Work size is number of locals/registers plus size of the maximum
     * call site argument list. */
    static_frame_body->work_size = sizeof(MVMRegister) *
        (static_frame_body->num_locals + static_frame_body->cu->body.max_callsite_size);

    /* Validate the bytecode. */
    MVM_validate_static_frame(tc, static_frame);

    /* Obtain an index to each threadcontext's pool table */
    static_frame_body->pool_index = MVM_incr(&tc->instance->num_frame_pools);
    if (static_frame_body->pool_index >= tc->frame_pool_table_size) {
        /* Grow the threadcontext's pool table */
        MVMuint32 old_size = tc->frame_pool_table_size;
        MVMuint32 new_size = tc->frame_pool_table_size;
        do {
            new_size *= 2;
        } while (static_frame_body->pool_index >= new_size);

        tc->frame_pool_table = realloc(tc->frame_pool_table,
            new_size * sizeof(MVMFrame *));
        memset(tc->frame_pool_table + old_size, 0,
            (new_size - old_size) * sizeof(MVMFrame *));
        tc->frame_pool_table_size = new_size;
    }

    /* Mark frame as invoked, so we need not do these calculations again. */
    static_frame_body->invoked = 1;
}

/* Increases the reference count of a frame. */
MVMFrame * MVM_frame_inc_ref(MVMThreadContext *tc, MVMFrame *frame) {
    MVM_incr(&frame->ref_count);
    return frame;
}

/* Decreases the reference count of a frame. If it hits zero, then we can
 * free it. Returns null for convenience. */
MVMFrame * MVM_frame_dec_ref(MVMThreadContext *tc, MVMFrame *frame) {
    /* MVM_dec returns what the count was before it decremented it
     * to zero, so we look for 1 here. */
    while (MVM_decr(&frame->ref_count) == 1) {
        MVMuint32 pool_index = frame->static_info->body.pool_index;
        MVMFrame *node = tc->frame_pool_table[pool_index];
        MVMFrame *outer_to_decr = frame->outer;

        /* If there's a caller pointer, decrement that. */
        if (frame->caller)
            frame->caller = MVM_frame_dec_ref(tc, frame->caller);

        if (node && MVM_load(&node->ref_count) >= MVMFramePoolLengthLimit) {
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
            MVM_store(&frame->ref_count, (frame->outer = node) ? MVM_load(&node->ref_count) + 1 : 1);
            tc->frame_pool_table[pool_index] = frame;
        }
        if (outer_to_decr)
            frame = outer_to_decr; /* and loop */
        else
            break;
    }
    return NULL;
}

/* Provides auto-close functionality, for the handful of cases where we have
 * not ever been in the outer frame of something we're invoking. In this case,
 * we fake up a frame based on the static lexical environment. */
MVMFrame * autoclose(MVMThreadContext *tc, MVMStaticFrame *needed) {
    MVMFrame *result;

    /* First, see if we can find one on the call stack; return it if so. */
    MVMFrame *candidate = tc->cur_frame;
    while (candidate) {
        if (candidate->static_info->body.bytecode == needed->body.bytecode)
            return candidate;
        candidate = candidate->caller;
    }

    /* If not, fake up a frame See if it also needs an outer. */
    result = MVM_frame_create_context_only(tc, needed, (MVMObject *)needed->body.static_code);
    if (needed->body.outer) {
        /* See if the static code object has an outer. */
        MVMCode *outer_code = needed->body.outer->body.static_code;
        if (outer_code->body.outer) {
            /* Yes, just take it. */
            result->outer = MVM_frame_inc_ref(tc, outer_code->body.outer);
        }
        else {
            /* Otherwise, recursively auto-close. */
            result->outer = MVM_frame_inc_ref(tc, autoclose(tc, needed->body.outer));
        }
    }
    return result;
}

/* Takes a static frame and a thread context. Invokes the static frame. */
void MVM_frame_invoke(MVMThreadContext *tc, MVMStaticFrame *static_frame,
                      MVMCallsite *callsite, MVMRegister *args,
                      MVMFrame *outer, MVMObject *code_ref) {
    MVMFrame *frame;

    MVMuint32 pool_index;
    MVMFrame *node;
    int fresh = 0;
    MVMStaticFrameBody *static_frame_body = &static_frame->body;

    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame_body->invoked)
        prepare_and_verify_static_frame(tc, static_frame);

    pool_index = static_frame_body->pool_index;
    node = tc->frame_pool_table[pool_index];

    if (node == NULL) {
        fresh = 1;
        frame = malloc(sizeof(MVMFrame));
        frame->params.named_used = NULL;

        /* Ensure special return pointer is null. */
        frame->special_return = NULL;
    }
    else {
        tc->frame_pool_table[pool_index] = node->outer;
        node->outer = NULL;
        frame = node;
    }

    /* Copy thread context (back?) into the frame. */
    frame->tc = tc;

    /* Set static frame. */
    frame->static_info = static_frame;

    /* Store the code ref (NULL at the top-level). */
    frame->code_ref = code_ref;

    /* Allocate space for lexicals and work area, copying the default lexical
     * environment into place. */
    if (static_frame_body->env_size) {
        if (fresh)
            frame->env = malloc(static_frame_body->env_size);
        memcpy(frame->env, static_frame_body->static_env, static_frame_body->env_size);
    }
    else {
        frame->env = NULL;
    }
    if (static_frame_body->work_size) {
        if (fresh || !frame->work)
            frame->work = malloc(static_frame_body->work_size);
        memset(frame->work, 0, static_frame_body->work_size);
    }
    else {
        frame->work = NULL;
    }

    /* Calculate args buffer position and make sure current call site starts
     * empty. */
    frame->args = static_frame_body->work_size ?
        frame->work + static_frame_body->num_locals :
        NULL;
    frame->cur_args_callsite = NULL;

    /* Outer. */
    if (outer) {
        /* We were provided with an outer frame; just ensure that it is
         * based on the correct static frame (compare on bytecode address
         * to come with nqp::freshcoderef). */
        if (outer->static_info->body.bytecode == static_frame_body->outer->body.bytecode)
            frame->outer = outer;
        else
            MVM_exception_throw_adhoc(tc,
                "When invoking %s, Provided outer frame %p (%s %s) does not match expected static frame type %p (%s %s)",
                static_frame_body->name ? MVM_string_utf8_encode_C_string(tc, static_frame_body->name) : "<anonymous static frame>",
                outer->static_info,
                MVM_repr_get_by_id(tc, REPR(outer->static_info)->ID)->name,
                outer->static_info->body.name ? MVM_string_utf8_encode_C_string(tc, outer->static_info->body.name) : "<anonymous static frame>",
                static_frame_body->outer,
                MVM_repr_get_by_id(tc, REPR(static_frame_body->outer)->ID)->name,
                static_frame_body->outer->body.name ? MVM_string_utf8_encode_C_string(tc, static_frame_body->outer->body.name) : "<anonymous static frame>");
    }
    else if (static_frame_body->static_code && static_frame_body->static_code->body.outer) {
        /* We're lacking an outer, but our static code object may have one.
         * This comes up in the case of cloned protoregexes, for example. */
        frame->outer = static_frame_body->static_code->body.outer;
    }
    else if (static_frame_body->outer) {
        frame->outer = autoclose(tc, static_frame_body->outer);
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
    frame->keep_caller = 0;

    /* Initial reference count is 1 by virtue of it being the currently
     * executing frame. */
    MVM_store(&frame->ref_count, 1);
    MVM_store(&frame->gc_seq_number, 0);

    /* Initialize argument processing. */
    MVM_args_proc_init(tc, &frame->params, callsite, args);
    
    /* Make sure there's no frame context pointer. */
    frame->context_object = NULL;

    /* Update interpreter and thread context, so next execution will use this
     * frame. */
    tc->cur_frame = frame;
    *(tc->interp_cur_op) = static_frame_body->bytecode;
    *(tc->interp_bytecode_start) = static_frame_body->bytecode;
    *(tc->interp_reg_base) = frame->work;
    *(tc->interp_cu) = static_frame_body->cu;

    /* If we need to do so, make clones of things in the lexical environment
     * that need it. Note that we do this after tc->cur_frame became the
     * current frame, to make sure these new objects will certainly get
     * marked if GC is triggered along the way. */
    if (static_frame_body->static_env_flags) {
        /* Drag everything out of static_frame_body before we start,
         * as GC action may invalidate it. */
        MVMuint8 *flags = static_frame_body->static_env_flags;
        MVMint64 numlex = static_frame_body->num_lexicals;
        MVMint64 i;
        for (i = 0; i < numlex; i++) {
            switch (flags[i]) {
                case 0: break;
                case 1:
                    frame->env[i].o = MVM_repr_clone(tc, frame->env[i].o);
                    break;
                case 2:
                    frame->env[i].o = MVM_repr_clone(tc, frame->env[i].o);
                    break;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "Unknown lexical environment setup flag");
            }
        }
    }
}

/* Creates a frame that is suitable for deserializing a context into. Does not
 * try to use the frame pool, since we'll typically never recycle these. */
MVMFrame * MVM_frame_create_context_only(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMObject *code_ref) {
    MVMFrame *frame;

    /* If the frame was never invoked before, need initial calculations
     * and verification. */
    if (!static_frame->body.invoked)
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
    if (static_frame->body.env_size) {
        frame->env = malloc(static_frame->body.env_size);
        memcpy(frame->env, static_frame->body.static_env, static_frame->body.env_size);
    }

    /* Initial reference count is 0 (will become referenced by being set as
     * an outer context). So just return it now. */
    return frame;
}

/* Return/unwind do about the same thing; this factors it out. */
static MVMuint64 return_or_unwind(MVMThreadContext *tc, MVMuint8 unwind) {
    MVMFrame *returner = tc->cur_frame;
    MVMFrame *caller   = returner->caller;
    MVMint64  retval;

    /* Arguments buffer no longer in use (saves GC visiting it). */
    returner->cur_args_callsite = NULL;

    /* Clear up argument processing leftovers, if any. */
    if (returner->work) {
        MVM_args_proc_cleanup_for_cache(tc, &returner->params);
    }

    /* Signal to the GC to ignore ->work */
    returner->tc = NULL;

    /* Unless we need to keep the caller chain in place, clear it up. */
    if (caller) {
        if (!returner->keep_caller) {
            MVM_frame_dec_ref(tc, caller);
            returner->caller = NULL;
        }
        else if (unwind) {
            caller->keep_caller = 1;
        }
    }

    /* Switch back to the caller frame if there is one. */
    if (caller && returner != tc->thread_entry_frame) {
        tc->cur_frame = caller;
        *(tc->interp_cur_op) = caller->return_address;
        *(tc->interp_bytecode_start) = caller->static_info->body.bytecode;
        *(tc->interp_reg_base) = caller->work;
        *(tc->interp_cu) = caller->static_info->body.cu;

        /* Handle any special return hooks. */
        if (caller->special_return) {
            MVMSpecialReturn sr = caller->special_return;
            caller->special_return = NULL;
            if (!unwind)
                sr(tc, caller->special_return_data);
        }

        retval = 1;
    }
    else {
        tc->cur_frame = NULL;
        retval = 0;
    }

    /* Decrement the frame's ref-count by the 1 it got by virtue of being the
     * currently executing frame. */
    MVM_frame_dec_ref(tc, returner);

    return retval;
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

/* Given the specified code object, sets its outer to the current scope. */
void MVM_frame_capturelex(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *code_obj;

    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform capturelex on object with representation MVMCode");

    /* XXX Following is vulnerable to a race condition. */
    code_obj = (MVMCode *)code;
    if (code_obj->body.outer)
        MVM_frame_dec_ref(tc, code_obj->body.outer);
    code_obj->body.outer = MVM_frame_inc_ref(tc, tc->cur_frame);
}

/* Given the specified code object, copies it and returns a copy which
 * captures a closure over the current scope. */
MVMObject * MVM_frame_takeclosure(MVMThreadContext *tc, MVMObject *code) {
    MVMCode *closure;

    if (REPR(code)->ID != MVM_REPR_ID_MVMCode)
        MVM_exception_throw_adhoc(tc,
            "Can only perform takeclosure on object with representation MVMCode");

    MVMROOT(tc, code, {
        closure = (MVMCode *)REPR(code)->allocate(tc, STABLE(code));
    });

    MVM_ASSIGN_REF(tc, closure, closure->body.sf, ((MVMCode *)code)->body.sf);
    closure->body.outer = MVM_frame_inc_ref(tc, tc->cur_frame);
    MVM_ASSIGN_REF(tc, closure, closure->body.code_object, ((MVMCode *)code)->body.code_object);

    return (MVMObject *)closure;
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. Non-existing object lexicals produce NULL, expected
 * (for better or worse) by various things. Otherwise, an error is thrown
 * if it does not exist. Incorrect type always throws. */
MVMRegister * MVM_frame_find_lexical_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 type) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
            MVMLexicalRegistry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == type)
                    return &cur_frame->env[entry->value];
                else
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name '%s' has wrong type",
                            MVM_string_utf8_encode_C_string(tc, name));
            }
        }
        cur_frame = cur_frame->outer;
    }
    if (type == MVM_reg_obj)
        return NULL;
    MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. Only works if it's an object lexical.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
            MVMLexicalRegistry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == MVM_reg_obj)
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

/* Looks up the address of the lexical with the specified name, starting with
 * the specified frame. It checks all outer frames of the caller frame chain.  */
MVMRegister * MVM_frame_find_lexical_by_name_rel_caller(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_caller_frame) {
    MVM_string_flatten(tc, name);
    while (cur_caller_frame != NULL) {
        MVMFrame *cur_frame = cur_caller_frame;
        while (cur_frame != NULL) {
            MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
            if (lexical_names) {
                /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
                MVMLexicalRegistry *entry;

                MVM_HASH_GET(tc, lexical_names, name, entry)

                if (entry) {
                    if (cur_frame->static_info->body.lexical_types[entry->value] == MVM_reg_obj)
                        return &cur_frame->env[entry->value];
                    else
                       MVM_exception_throw_adhoc(tc,
                            "Lexical with name '%s' has wrong type",
                                MVM_string_utf8_encode_C_string(tc, name));
                }
            }
            cur_frame = cur_frame->outer;
        }
        cur_caller_frame = cur_caller_frame->caller;
    }
    MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}

/* Looks up the address of the lexical with the specified name and the
 * specified type. Returns null if it does not exist. */
MVMRegister * MVM_frame_find_contextual_by_name(MVMThreadContext *tc, MVMString *name, MVMuint16 *type, MVMFrame *cur_frame) {
    if (!name) {
        MVM_exception_throw_adhoc(tc, "Contextual name cannot be null");
    }
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            MVMLexicalRegistry *entry;

            MVM_HASH_GET(tc, lexical_names, name, entry)

            if (entry) {
                *type = cur_frame->static_info->body.lexical_types[entry->value];
                return &cur_frame->env[entry->value];
            }
        }
        cur_frame = cur_frame->caller;
    }
    return NULL;
}

MVMObject * MVM_frame_getdynlex(MVMThreadContext *tc, MVMString *name, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame);
    MVMObject *result = NULL, *result_type = NULL;
    if (lex_reg) {
        switch (type) {
            case MVM_reg_int64:
                result_type = (*tc->interp_cu)->body.hll_config->int_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing int box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_int(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->i64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_num64:
                result_type = (*tc->interp_cu)->body.hll_config->num_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing num box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_num(tc, STABLE(result), result,
                    OBJECT_BODY(result), lex_reg->n64);
                MVM_gc_root_temp_pop(tc);
                break;
            case MVM_reg_str:
                result_type = (*tc->interp_cu)->body.hll_config->str_box_type;
                if (!result_type)
                    MVM_exception_throw_adhoc(tc, "missing str box type");
                result = REPR(result_type)->allocate(tc, STABLE(result_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                if (REPR(result)->initialize)
                    REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
                REPR(result)->box_funcs.set_str(tc, STABLE(result), result,
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

void MVM_frame_binddynlex(MVMThreadContext *tc, MVMString *name, MVMObject *value, MVMFrame *cur_frame) {
    MVMuint16 type;
    MVMRegister *lex_reg = MVM_frame_find_contextual_by_name(tc, name, &type, cur_frame);
    if (!lex_reg) {
        MVM_exception_throw_adhoc(tc, "No contextual found with name '%s'",
            MVM_string_utf8_encode_C_string(tc, name));
    }
    switch (type) {
        case MVM_reg_int64:
            lex_reg->i64 = REPR(value)->box_funcs.get_int(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_num64:
            lex_reg->n64 = REPR(value)->box_funcs.get_num(tc,
                STABLE(value), value, OBJECT_BODY(value));
            break;
        case MVM_reg_str:
            lex_reg->s = REPR(value)->box_funcs.get_str(tc,
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
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
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
    MVMLexicalRegistry *lexical_names = f->static_info->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
        MVM_string_flatten(tc, name);
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry) {
            switch (f->static_info->body.lexical_types[entry->value]) {
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

MVMObject * MVM_frame_find_invokee(MVMThreadContext *tc, MVMObject *code, MVMCallsite **tweak_cs) {
    if (!code)
        MVM_exception_throw_adhoc(tc, "Cannot invoke null object");
    if (STABLE(code)->invoke == MVM_6model_invoke_default) {
        MVMInvocationSpec *is = STABLE(code)->invocation_spec;
        if (!is) {
            MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s, cs = %d)",
                REPR(code)->name, STABLE(code)->container_spec ? 1 : 0);
        }
        if (is->class_handle) {
            MVMRegister dest;
            REPR(code)->attr_funcs.get_attribute(tc,
                STABLE(code), code, OBJECT_BODY(code),
                is->class_handle, is->attr_name,
                is->hint, &dest, MVM_reg_obj);
            code = dest.o;
        }
        else {
            /* Need to tweak the callsite and args to include the code object
             * being invoked. */
            if (tweak_cs) {
                MVMCallsite *orig = *tweak_cs;
                if (orig->with_invocant) {
                    *tweak_cs = orig->with_invocant;
                }
                else {
                    MVMCallsite *new  = malloc(sizeof(MVMCallsite));
                    new->arg_flags    = malloc((orig->arg_count + 1) * sizeof(MVMCallsiteEntry));
                    new->arg_flags[0] = MVM_CALLSITE_ARG_OBJ;
                    memcpy(new->arg_flags + 1, orig->arg_flags, orig->arg_count);
                    new->arg_count      = orig->arg_count + 1;
                    new->num_pos        = orig->num_pos + 1;
                    new->has_flattening = orig->has_flattening;
                    new->with_invocant  = NULL;
                    *tweak_cs = orig->with_invocant = new;
                }
                memmove(tc->cur_frame->args + 1, tc->cur_frame->args,
                    orig->arg_count * sizeof(MVMRegister));
                tc->cur_frame->args[0].o = code;
            }
            else {
                MVM_exception_throw_adhoc(tc,
                    "Cannot invoke object with invocation handler in this context");
            }
            code = is->invocation_handler;
        }
    }
    return code;
}

MVMObject * MVM_frame_context_wrapper(MVMThreadContext *tc, MVMFrame *f) {
    MVMObject *ctx = (MVMObject *)MVM_load(&f->context_object);

    if (!ctx) {
        ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
        ((MVMContext *)ctx)->body.context = MVM_frame_inc_ref(tc, f);

        if (MVM_casptr(&f->context_object, NULL, ctx) != NULL) {
            ((MVMContext *)ctx)->body.context = MVM_frame_dec_ref(tc, f);
            ctx = (MVMObject *)MVM_load(&f->context_object);
        }
    }

    return ctx;
}
