#include "moarvm.h"

/* Struct used internally in here. */
struct MVMArgInfo {
    MVMRegister      *arg;
    MVMCallsiteEntry  flags;
};

/* Initialize arguments processing context. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args) {
    /* Stash callsite and argument counts/pointers. */
    ctx->callsite = callsite;
    /* initial counts and values; can be altered by flatteners */
    ctx->args     = args;
    if (ctx->named_used && ctx->named_used_size >= (callsite->arg_count - callsite->num_pos) / 2) { /* reuse the old one */
        memset(ctx->named_used, 0, ctx->named_used_size);
    }
    else {
        if (ctx->named_used) {
            free(ctx->named_used);
            ctx->named_used = NULL;
        }
        ctx->named_used_size = (callsite->arg_count - callsite->num_pos) / 2;
        ctx->named_used = ctx->named_used_size ? calloc(sizeof(MVMuint8), ctx->named_used_size) : NULL;
    }
    ctx->num_pos  = callsite->num_pos;
    ctx->arg_count = callsite->arg_count;
    ctx->arg_flags = NULL; /* will be populated by flattener if needed */
}

/* Clean up an arguments processing context for cache. */
void MVM_args_proc_cleanup_for_cache(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    /* Really, just if ctx->arg_flags, which indicates a flattening occurred. */
    if (ctx->callsite->has_flattening) {
        if (ctx->args) {
            free(ctx->args);
            ctx->args = NULL;
        }
        if (ctx->arg_flags) {
            free(ctx->arg_flags);
            ctx->arg_flags = NULL;
        }
    }
}

/* Clean up an arguments processing context. */
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVM_args_proc_cleanup_for_cache(tc, ctx);
    if (ctx->named_used) {
        free(ctx->named_used);
        ctx->named_used = NULL;
        ctx->named_used_size = 0;
    }
}

static const char * get_arg_type_name(MVMThreadContext *tc, MVMuint8 type) {
    if (type & MVM_CALLSITE_ARG_OBJ)  return "object";
    if (type & MVM_CALLSITE_ARG_INT)  return "integer";
    if (type & MVM_CALLSITE_ARG_NUM)  return "number";
    if (type & MVM_CALLSITE_ARG_STR)  return "string";
    MVM_exception_throw_adhoc(tc, "invalid arg type");
}

static void flatten_args(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Checks that the passed arguments fall within the expected arity. */
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max) {
    MVMuint16 num_pos = ctx->num_pos;
    if (num_pos < min)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed %u, got %u", min, num_pos);
    if (num_pos > max)
        MVM_exception_throw_adhoc(tc, "Too many positional arguments; max %u, got %u", max, num_pos);
    
    flatten_args(tc, ctx);
}

/* Get positional arguments. */
static struct MVMArgInfo find_pos_arg(MVMArgProcContext *ctx, MVMuint32 pos) {
    struct MVMArgInfo result;
    if (pos < ctx->num_pos) {
        result.arg = &ctx->args[pos];
        result.flags = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[pos];
    }
    else {
        result.arg = NULL;
    }
    return result;
}
MVMRegister * MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1);
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_OBJ))
        MVM_exception_throw_adhoc(tc, "Expected object, got %s", get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1);
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_INT))
        MVM_exception_throw_adhoc(tc, "Expected integer, got %s", get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1);
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_NUM))
        MVM_exception_throw_adhoc(tc, "Expected number, got %s", get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1);
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Expected string, got %s", get_arg_type_name(tc, result.flags));
    return result.arg;
}

/* Get named arguments. */
static struct MVMArgInfo find_named_arg(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name) {
    struct MVMArgInfo result;
    MVMuint32 flag_pos, arg_pos;
    result.arg = NULL;
    
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) {
        if (MVM_string_equal(tc, ctx->args[arg_pos].s, name)) {
            result.arg = &ctx->args[arg_pos + 1];
            result.flags = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[flag_pos];
            /* Mark this named taken so a slurpy won't get it. */
            ctx->named_used[(arg_pos - ctx->num_pos)/2] = 1;
            break;
        }
    }
    
    return result;
}
MVMRegister * MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Required named object argument missing: %s", MVM_string_utf8_encode_C_string(tc, name));
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_OBJ))
        MVM_exception_throw_adhoc(tc, "Expected object for named argument %s, got %s",
            MVM_string_utf8_encode_C_string(tc, name), get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Required named integer argument missing: %s", MVM_string_utf8_encode_C_string(tc, name));
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_INT))
        MVM_exception_throw_adhoc(tc, "Expected integer for named argument %s, got %s",
            MVM_string_utf8_encode_C_string(tc, name), get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Required named number argument missing: %s", MVM_string_utf8_encode_C_string(tc, name));
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_NUM))
        MVM_exception_throw_adhoc(tc, "Expected number for named argument %s, got %s",
            MVM_string_utf8_encode_C_string(tc, name), get_arg_type_name(tc, result.flags));
    return result.arg;
}
MVMRegister * MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Required named string argument missing: %s", MVM_string_utf8_encode_C_string(tc, name));
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Expected string for named argument %s, got %s",
            MVM_string_utf8_encode_C_string(tc, name), get_arg_type_name(tc, result.flags));
    return result.arg;
}

/* Result setting. The frameless flag indicates that the currently
 * executing code does not have a MVMFrame of its own. */
void MVM_args_set_result_obj(MVMThreadContext *tc, MVMObject *result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            case MVM_RETURN_OBJ:
                target->return_value->o = result;
                break;
            case MVM_RETURN_INT:
                target->return_value->i64 = MVM_repr_get_int(tc, result);
                break;
            case MVM_RETURN_NUM:
                target->return_value->n64 = MVM_repr_get_num(tc, result);
                break;
            case MVM_RETURN_STR:
                target->return_value->s = MVM_repr_get_str(tc, result);
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion from obj NYI; expects type %u", target->return_type);
        }
    }
}
void MVM_args_set_result_int(MVMThreadContext *tc, MVMint64 result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            case MVM_RETURN_INT:
                target->return_value->i64 = result;
                break;
            case MVM_RETURN_NUM:
                target->return_value->n64 = (MVMnum64)result;
                break;
            case MVM_RETURN_OBJ: {
                MVMObject *box, *box_type;
                box_type = target->static_info->cu->hll_config->int_box_type;
                box = REPR(box_type)->allocate(tc, STABLE(box_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_int(tc, STABLE(box), box, OBJECT_BODY(box), result);
                MVM_gc_root_temp_pop(tc);
                target->return_value->o = box;
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion from int NYI; expects type %u", target->return_type);
        }
    }
}
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            case MVM_RETURN_NUM:
                target->return_value->n64 = result;
                break;
            case MVM_RETURN_INT:
                target->return_value->i64 = (MVMint64)result;
                break;
            case MVM_RETURN_OBJ: {
                MVMObject *box, *box_type;
                box_type = target->static_info->cu->hll_config->num_box_type;
                box = REPR(box_type)->allocate(tc, STABLE(box_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_num(tc, STABLE(box), box, OBJECT_BODY(box), result);
                MVM_gc_root_temp_pop(tc);
                target->return_value->o = box;
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion from num NYI; expects type %u", target->return_type);
        }
    }
}
void MVM_args_set_result_str(MVMThreadContext *tc, MVMString *result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            case MVM_RETURN_STR:
                target->return_value->s = result;
                break;
            case MVM_RETURN_OBJ: {
                MVMObject *box, *box_type;
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
                box_type = target->static_info->cu->hll_config->str_box_type;
                box = REPR(box_type)->allocate(tc, STABLE(box_type));
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_str(tc, STABLE(box), box, OBJECT_BODY(box), result);
                MVM_gc_root_temp_pop_n(tc, 2);
                target->return_value->o = box;
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion from str NYI; expects type %u", target->return_type);
        }
    }
}
void MVM_args_assert_void_return_ok(MVMThreadContext *tc, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target && target->return_type != MVM_RETURN_VOID && tc->cur_frame != tc->thread_entry_frame)
        MVM_exception_throw_adhoc(tc, "Void return not allowed to context requiring a return value");
}

MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos) {
    MVMObject *type = (*(tc->interp_cu))->hll_config->slurpy_array_type, *result = NULL, *box = NULL;
    struct MVMArgInfo arg_info;
    MVMRegister reg;
    
    if (!type || IS_CONCRETE(type)) {
        MVM_exception_throw_adhoc(tc, "Missing hll slurpy array type");
    }
    
    result = REPR(type)->allocate(tc, STABLE(type));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
    if (REPR(result)->initialize)
        REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);
    
    arg_info = find_pos_arg(ctx, pos++);
    while (arg_info.arg) {
        
        if (arg_info.flags & MVM_CALLSITE_ARG_FLAT) {
            MVM_exception_throw_adhoc(tc, "Arg has not been flattened in slurpy_positional");
        }
        
        /* XXX theoretically needs to handle native arrays I guess */
        switch (arg_info.flags & MVM_CALLSITE_ARG_MASK) {
            case MVM_CALLSITE_ARG_OBJ: {
                REPR(result)->pos_funcs->push(tc, STABLE(result), result,
                    OBJECT_BODY(result), *arg_info.arg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_INT:{
                type = (*(tc->interp_cu))->hll_config->int_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll int box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_int(tc, STABLE(box), box,
                    OBJECT_BODY(box),
                    (arg_info.flags & MVM_CALLSITE_ARG_MASK == MVM_CALLSITE_ARG_INT
                        ? arg_info.arg->i64 : arg_info.arg->ui64));
                reg.o = box;
                REPR(result)->pos_funcs->push(tc, STABLE(result), result,
                    OBJECT_BODY(result), reg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                type = (*(tc->interp_cu))->hll_config->num_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll num box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_num(tc, STABLE(box), box,
                    OBJECT_BODY(box), arg_info.arg->n64);
                reg.o = box;
                REPR(result)->pos_funcs->push(tc, STABLE(result), result,
                    OBJECT_BODY(result), reg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                type = (*(tc->interp_cu))->hll_config->str_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll str box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_str(tc, STABLE(box), box,
                    OBJECT_BODY(box), arg_info.arg->s);
                reg.o = box;
                REPR(result)->pos_funcs->push(tc, STABLE(result), result,
                    OBJECT_BODY(result), reg, MVM_reg_obj);
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "arg flag is empty in slurpy positional");
        }
        
        arg_info = find_pos_arg(ctx, pos++);
        if (pos == 1) break; /* overflow?! */
    }
    
    MVM_gc_root_temp_pop_n(tc, 2);
    
    return result;
}

MVMObject * MVM_args_slurpy_named(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMObject *type = (*(tc->interp_cu))->hll_config->slurpy_hash_type, *result = NULL, *box = NULL;
    struct MVMArgInfo arg_info;
    MVMuint32 flag_pos, arg_pos;
    arg_info.arg = NULL;
    
    if (!type || IS_CONCRETE(type)) {
        MVM_exception_throw_adhoc(tc, "Missing hll slurpy hash type");
    }
    
    result = REPR(type)->allocate(tc, STABLE(type));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
    if (REPR(result)->initialize)
        REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);
    
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) {
        MVMString *key;
        
        if (ctx->named_used[flag_pos - ctx->num_pos]) continue;
        
        key = ctx->args[arg_pos].s;
        
        if (!key || !IS_CONCRETE(key)) {
            MVM_exception_throw_adhoc(tc, "slurpy hash needs concrete key");
        }
        arg_info.arg = &ctx->args[arg_pos + 1];
        arg_info.flags = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[flag_pos];
        
        if (arg_info.flags & MVM_CALLSITE_ARG_FLAT) {
            MVM_exception_throw_adhoc(tc, "Arg has not been flattened in slurpy_named");
        }
        
        switch (arg_info.flags & MVM_CALLSITE_ARG_MASK) {
            case MVM_CALLSITE_ARG_OBJ: {
                REPR(result)->ass_funcs->bind_key_boxed(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, arg_info.arg->o);
                break;
            }
            case MVM_CALLSITE_ARG_INT: {
                type = (*(tc->interp_cu))->hll_config->int_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll int box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_int(tc, STABLE(box), box,
                    OBJECT_BODY(box),
                    (arg_info.flags & MVM_CALLSITE_ARG_MASK == MVM_CALLSITE_ARG_INT
                        ? arg_info.arg->i64 : arg_info.arg->ui64));
                REPR(result)->ass_funcs->bind_key_boxed(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, box);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                type = (*(tc->interp_cu))->hll_config->num_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll num box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_num(tc, STABLE(box), box,
                    OBJECT_BODY(box), arg_info.arg->n64);
                REPR(result)->ass_funcs->bind_key_boxed(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, box);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                type = (*(tc->interp_cu))->hll_config->str_box_type;
                if (!type || IS_CONCRETE(type)) {
                    MVM_exception_throw_adhoc(tc, "Missing hll str box type");
                }
                box = REPR(type)->allocate(tc, STABLE(type));
                if (REPR(box)->initialize)
                    REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box));
                REPR(box)->box_funcs->set_str(tc, STABLE(box), box,
                    OBJECT_BODY(box), arg_info.arg->s);
                REPR(result)->ass_funcs->bind_key_boxed(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, box);
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "arg flag is empty in slurpy named");
        }
    }
    
    MVM_gc_root_temp_pop_n(tc, 2);
    
    return result;
}

static void flatten_args(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    struct MVMArgInfo arg_info;
    MVMuint16 flag_pos = 0, arg_pos = 0, new_arg_pos = 0,
        new_arg_flags_size = ctx->arg_count > 0x7FFF ? ctx->arg_count : ctx->arg_count * 2,
        new_args_size = new_arg_flags_size, i, new_flag_pos = 0, new_num_pos = 0;
    MVMCallsiteEntry *new_arg_flags;
    MVMRegister *new_args;
    
    if (!ctx->callsite->has_flattening) return;
    
    new_arg_flags = malloc(new_arg_flags_size);
    new_args = malloc(new_args_size);
    
    /* first flatten any positionals */
    for ( ; arg_pos < ctx->num_pos; arg_pos++) {
        MVMuint32 found = 0;
        
        arg_info.arg = &ctx->args[arg_pos];
        arg_info.flags = ctx->callsite->arg_flags[arg_pos];
        
        /* skip it if it's not flattening or is null. The bytecode loader
         * verifies it's a MVM_CALLSITE_ARG_OBJ. */
        if ((arg_info.flags & MVM_CALLSITE_ARG_FLAT) && arg_info.arg->o) {
            MVMObject *list = arg_info.arg->o;
            
            MVMint64 count = REPR(list)->pos_funcs->elems(tc, STABLE(list),
                list, OBJECT_BODY(list));
            if ((MVMint64)new_arg_pos + count > 0xFFFF) {
                MVM_exception_throw_adhoc(tc, "Too many arguments in flattening array.");
            }
            found = (MVMuint32)count;
            
            for (i = 0; i < found; i++) {
                if (new_arg_pos == new_args_size) {
                    new_args = realloc(new_args, (new_args_size *= 2));
                }
                if (new_flag_pos == new_arg_flags_size) {
                    new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2));
                }
                
                REPR(list)->pos_funcs->at_pos(tc, STABLE(list), list,
                    OBJECT_BODY(list), i, new_args + new_arg_pos++, MVM_reg_obj);
                new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_OBJ;
            }
        }
        else {
            if (new_arg_pos == new_args_size) {
                new_args = realloc(new_args, (new_args_size *= 2));
            }
            if (new_flag_pos == new_arg_flags_size) {
                new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2));
            }
            
            (new_args + new_arg_pos++)->o = arg_info.arg->o;
            new_arg_flags[new_flag_pos++] = arg_info.flags;
            found = 1;
        }
        
        new_arg_pos += found;
    }
    
    /* then flatten any nameds */
    for ( flag_pos = arg_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) {
        
        
    }
    
    ctx->args = new_args;
    ctx->arg_count = new_arg_pos;
    ctx->num_pos = new_num_pos;
    ctx->arg_flags = new_arg_flags;
}
