#include "moarvm.h"

static void init_named_used(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 num) {
    if (ctx->named_used && ctx->named_used_size >= num) { /* reuse the old one */
        memset(ctx->named_used, 0, ctx->named_used_size * sizeof(MVMuint8));
    }
    else {
        if (ctx->named_used) {
            free(ctx->named_used);
            ctx->named_used = NULL;
        }
        ctx->named_used_size = num;
        ctx->named_used = ctx->named_used_size ? calloc(sizeof(MVMuint8), ctx->named_used_size) : NULL;
    }
}

/* Initialize arguments processing context. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args) {
    /* Stash callsite and argument counts/pointers. */
    ctx->callsite = callsite;
    /* initial counts and values; can be altered by flatteners */
    init_named_used(tc, ctx, (callsite->arg_count - callsite->num_pos) / 2);
    ctx->args     = args;
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

static void flatten_args(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Checks that the passed arguments fall within the expected arity. */
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max) {
    MVMuint16 num_pos;

    flatten_args(tc, ctx);

    num_pos = ctx->num_pos;
    if (num_pos < min)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed %u, got %u", min, num_pos);
    if (num_pos > max)
        MVM_exception_throw_adhoc(tc, "Too many positional arguments; max %u, got %u", max, num_pos);
}

/* Get positional arguments. */
#define find_pos_arg(ctx, pos, result) do { \
    if (pos < ctx->num_pos) { \
        result.arg   = ctx->args[pos]; \
        result.flags = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[pos]; \
        result.exists = 1; \
    } \
    else { \
        result.exists = 0; \
    } \
} while (0)

#define autounbox(tc, type_flag, expected, result) do { \
    if (result.exists && !(result.flags & type_flag)) { \
        if (result.flags & MVM_CALLSITE_ARG_OBJ) { \
            MVMObject *obj; \
            MVMStorageSpec ss; \
            obj = result.arg.o; \
            ss = REPR(obj)->get_storage_spec(tc, STABLE(obj)); \
            switch (ss.can_box & MVM_STORAGE_SPEC_CAN_BOX_MASK) { \
                case MVM_STORAGE_SPEC_CAN_BOX_INT: \
                    result.arg.i64 = MVM_repr_get_int(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_INT; \
                    break; \
                case MVM_STORAGE_SPEC_CAN_BOX_NUM: \
                    result.arg.n64 = MVM_repr_get_num(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_NUM; \
                    break; \
                case MVM_STORAGE_SPEC_CAN_BOX_STR: \
                    result.arg.s = MVM_repr_get_str(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_STR; \
                    break; \
                default: \
                    MVM_exception_throw_adhoc(tc, "Failed to unbox object to " expected); \
            } \
        } \
        if (!(result.flags & type_flag)) { \
            switch (type_flag) { \
                case MVM_CALLSITE_ARG_OBJ: \
                    MVM_exception_throw_adhoc(tc, "unreachable unbox 0"); \
                case MVM_CALLSITE_ARG_INT: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_OBJ: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 1"); \
                        case MVM_CALLSITE_ARG_INT: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 2"); \
                        case MVM_CALLSITE_ARG_NUM: \
                            result.arg.i64 = (MVMint64)result.arg.n64; \
                            break; \
                        case MVM_CALLSITE_ARG_STR: \
                            MVM_exception_throw_adhoc(tc, "coerce string to int NYI"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 3"); \
                    } \
                    result.flags = MVM_CALLSITE_ARG_INT; \
                    break; \
                case MVM_CALLSITE_ARG_NUM: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_OBJ: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 4"); \
                        case MVM_CALLSITE_ARG_INT: \
                            result.arg.n64 = (MVMnum64)result.arg.i64; \
                            break; \
                        case MVM_CALLSITE_ARG_NUM: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 5"); \
                        case MVM_CALLSITE_ARG_STR: \
                            MVM_exception_throw_adhoc(tc, "coerce string to num NYI"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 6"); \
                    } \
                    result.flags = MVM_CALLSITE_ARG_NUM; \
                    break; \
                case MVM_CALLSITE_ARG_STR: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_OBJ: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 7"); \
                        case MVM_CALLSITE_ARG_INT: \
                            MVM_exception_throw_adhoc(tc, "coerce int to string NYI"); \
                        case MVM_CALLSITE_ARG_NUM: \
                            MVM_exception_throw_adhoc(tc, "coerce num to string NYI"); \
                        case MVM_CALLSITE_ARG_STR: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 8"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 9"); \
                    } \
                    result.flags = MVM_CALLSITE_ARG_STR; \
                    break; \
                default: \
                    MVM_exception_throw_adhoc(tc, "unreachable unbox 10"); \
            } \
        } \
    } \
} while (0)

#define args_get_pos(tc, ctx, pos, required, result) do { \
    find_pos_arg(ctx, pos, result); \
    if (!result.exists && required) { \
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1); \
    } \
} while (0)

#define autobox(tc, target, result, box_type_obj, is_object, set_func, dest) do { \
    MVMObject *box, *box_type; \
    box_type = target->static_info->cu->hll_config->box_type_obj; \
    box = REPR(box_type)->allocate(tc, STABLE(box_type)); \
    if (is_object) MVM_gc_root_temp_push(tc, (MVMCollectable **)&result); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&box); \
    if (REPR(box)->initialize) \
        REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
    REPR(box)->box_funcs->set_func(tc, STABLE(box), box, OBJECT_BODY(box), result); \
    if (is_object) MVM_gc_root_temp_pop_n(tc, 2); \
    else MVM_gc_root_temp_pop(tc); \
    dest = box; \
} while (0)

#define autobox_switch(tc, result) do { \
    if (result.exists) { \
        switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
            case MVM_CALLSITE_ARG_OBJ: \
                break; \
            case MVM_CALLSITE_ARG_INT: \
                autobox(tc, tc->cur_frame, result.arg.i64, int_box_type, 0, set_int, result.arg.o); \
                break; \
            case MVM_CALLSITE_ARG_NUM: \
                autobox(tc, tc->cur_frame, result.arg.n64, num_box_type, 0, set_num, result.arg.o); \
                break; \
            case MVM_CALLSITE_ARG_STR: \
                autobox(tc, tc->cur_frame, result.arg.s, str_box_type, 1, set_str, result.arg.o); \
                break; \
            default: \
                MVM_exception_throw_adhoc(tc, "invalid type flag"); \
        } \
    } \
} while (0)

MVMArgInfo MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, required, result);
    autobox_switch(tc, result);
    return result;
}
MVMArgInfo MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, required, result);
    autounbox(tc, MVM_CALLSITE_ARG_INT, "integer", result);
    return result;
}
MVMArgInfo MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, required, result);
    autounbox(tc, MVM_CALLSITE_ARG_NUM, "number", result);
    return result;
}
MVMArgInfo MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, required, result);
    autounbox(tc, MVM_CALLSITE_ARG_STR, "string", result);
    return result;
}

#define args_get_named(tc, ctx, name, required, _type) do { \
     \
    MVMuint32 flag_pos, arg_pos; \
    result.exists = 0; \
     \
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) { \
        if (MVM_string_equal(tc, ctx->args[arg_pos].s, name)) { \
            if (ctx->named_used[(arg_pos - ctx->num_pos)/2]) { \
                MVM_exception_throw_adhoc(tc, "Named argument already used: %s", MVM_string_utf8_encode_C_string(tc, name)); \
            } \
            result.arg    = ctx->args[arg_pos + 1]; \
            result.flags  = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[flag_pos]; \
            result.exists = 1; \
            ctx->named_used[(arg_pos - ctx->num_pos)/2] = 1; \
            break; \
        } \
    } \
    if (!result.exists && required) \
        MVM_exception_throw_adhoc(tc, "Required named " _type " argument missing: %s", MVM_string_utf8_encode_C_string(tc, name)); \
     \
} while (0)

MVMArgInfo MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required, "object");
    autobox_switch(tc, result);
    return result;
}
MVMArgInfo MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required, "integer");
    autounbox(tc, MVM_CALLSITE_ARG_INT, "integer", result);
    return result;
}
MVMArgInfo MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required, "number");
    autounbox(tc, MVM_CALLSITE_ARG_NUM, "number", result);
    return result;
}
MVMArgInfo MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required, "string");
    autounbox(tc, MVM_CALLSITE_ARG_STR, "string", result);
    return result;
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
                autobox(tc, target, result, int_box_type, 0, set_int, target->return_value->o);
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
                autobox(tc, target, result, num_box_type, 0, set_num, target->return_value->o);
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
                autobox(tc, target, result, str_box_type, 1, set_str, target->return_value->o);
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

#define box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, box_type_obj, name, set_func, reg_member, stmt1, action_funcs, action_func, target1, target2) do { \
    type = (*(tc->interp_cu))->hll_config->box_type_obj; \
    if (!type || IS_CONCRETE(type)) { \
        MVM_exception_throw_adhoc(tc, "Missing hll " name " box type"); \
    } \
    box = REPR(type)->allocate(tc, STABLE(type)); \
    if (REPR(box)->initialize) \
        REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
    REPR(box)->box_funcs->set_func(tc, STABLE(box), box, \
        OBJECT_BODY(box), arg_info.arg.reg_member); \
    stmt1; \
    REPR(result)->action_funcs->action_func(tc, STABLE(result), result, \
        OBJECT_BODY(result), target1, target2); \
} while (0)

MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos) {
    MVMObject *type = (*(tc->interp_cu))->hll_config->slurpy_array_type, *result = NULL, *box = NULL;
    MVMArgInfo arg_info;
    MVMRegister reg;

    if (!type || IS_CONCRETE(type)) {
        MVM_exception_throw_adhoc(tc, "Missing hll slurpy array type");
    }

    result = REPR(type)->allocate(tc, STABLE(type));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&result);
    if (REPR(result)->initialize)
        REPR(result)->initialize(tc, STABLE(result), result, OBJECT_BODY(result));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&box);

    find_pos_arg(ctx, pos, arg_info);
    pos++;
    while (arg_info.exists) {

        if (arg_info.flags & MVM_CALLSITE_ARG_FLAT) {
            MVM_exception_throw_adhoc(tc, "Arg has not been flattened in slurpy_positional");
        }

        /* XXX theoretically needs to handle native arrays I guess */
        switch (arg_info.flags & MVM_CALLSITE_ARG_MASK) {
            case MVM_CALLSITE_ARG_OBJ: {
                MVM_repr_push_o(tc, result, arg_info.arg.o);
                break;
            }
            case MVM_CALLSITE_ARG_INT:{
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, int_box_type, "int", set_int, i64, reg.o = box, pos_funcs, push, reg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, num_box_type, "num", set_num, n64, reg.o = box, pos_funcs, push, reg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, str_box_type, "str", set_str, s, reg.o = box, pos_funcs, push, reg, MVM_reg_obj);
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "arg flag is empty in slurpy positional");
        }

        find_pos_arg(ctx, pos, arg_info);
        pos++;
        if (pos == 1) break; /* overflow?! */
    }

    MVM_gc_root_temp_pop_n(tc, 2);

    return result;
}

MVMObject * MVM_args_slurpy_named(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMObject *type = (*(tc->interp_cu))->hll_config->slurpy_hash_type, *result = NULL, *box = NULL;
    MVMArgInfo arg_info;
    MVMuint32 flag_pos, arg_pos;
    arg_info.exists = 0;

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
        arg_info.arg    = ctx->args[arg_pos + 1];
        arg_info.flags  = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[flag_pos];
        arg_info.exists = 1;

        if (arg_info.flags & MVM_CALLSITE_ARG_FLAT) {
            MVM_exception_throw_adhoc(tc, "Arg has not been flattened in slurpy_named");
        }

        switch (arg_info.flags & MVM_CALLSITE_ARG_MASK) {
            case MVM_CALLSITE_ARG_OBJ: {
                REPR(result)->ass_funcs->bind_key_boxed(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, arg_info.arg.o);
                break;
            }
            case MVM_CALLSITE_ARG_INT: {
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, int_box_type, "int", set_int, i64, "", ass_funcs, bind_key_boxed, (MVMObject *)key, box);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, num_box_type, "num", set_num, n64, "", ass_funcs, bind_key_boxed, (MVMObject *)key, box);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                box_slurpy(tc, ctx, pos, type, result, box, arg_info, reg, str_box_type, "str", set_str, s, "", ass_funcs, bind_key_boxed, (MVMObject *)key, box);
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
    MVMArgInfo arg_info, key_info, val_info;
    MVMuint16 flag_pos = 0, arg_pos = 0, new_arg_pos = 0,
        new_arg_flags_size = ctx->arg_count > 0x7FFF ? ctx->arg_count : ctx->arg_count * 2,
        new_args_size = new_arg_flags_size, i, new_flag_pos = 0, new_num_pos = 0;
    MVMCallsiteEntry *new_arg_flags;
    MVMRegister *new_args;

    if (!ctx->callsite->has_flattening) return;

    new_arg_flags = malloc(new_arg_flags_size * sizeof(MVMCallsiteEntry));
    new_args = malloc(new_args_size * sizeof(MVMRegister));

    /* first flatten any positionals */
    for ( ; arg_pos < ctx->num_pos; arg_pos++) {

        arg_info.arg    = ctx->args[arg_pos];
        arg_info.flags  = ctx->callsite->arg_flags[arg_pos];
        arg_info.exists = 1;

        /* skip it if it's not flattening or is null. The bytecode loader
         * verifies it's a MVM_CALLSITE_ARG_OBJ. */
        if ((arg_info.flags & MVM_CALLSITE_ARG_FLAT) && arg_info.arg.o) {
            MVMObject *list = arg_info.arg.o;
            MVMint64 count;

            count = REPR(list)->elems(tc, STABLE(list),
                list, OBJECT_BODY(list));
            if ((MVMint64)new_arg_pos + count > 0xFFFF) {
                MVM_exception_throw_adhoc(tc, "Too many arguments in flattening array.");
            }

            for (i = 0; i < count; i++) {
                if (new_arg_pos == new_args_size) {
                    new_args = realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
                }
                if (new_flag_pos == new_arg_flags_size) {
                    new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
                }

                (new_args + new_arg_pos++)->o = MVM_repr_at_pos_o(tc, list, i);
                new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_OBJ;
            }
        }
        else if (!(arg_info.flags & MVM_CALLSITE_ARG_FLAT_NAMED)) {
            if (new_arg_pos == new_args_size) {
                new_args = realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
            }
            if (new_flag_pos == new_arg_flags_size) {
                new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
            }

            *(new_args + new_arg_pos++) = arg_info.arg;
            new_arg_flags[new_flag_pos++] = arg_info.flags;
        }
    }
    new_num_pos = new_arg_pos;

    /* then append any nameds from the original */
    for ( flag_pos = arg_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) {

        if (new_arg_pos + 1 >=  new_args_size) {
            new_args = realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
        }
        if (new_flag_pos == new_arg_flags_size) {
            new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
        }

        (new_args + new_arg_pos++)->s = (ctx->args + arg_pos)->s;
        *(new_args + new_arg_pos++) = *(ctx->args + arg_pos + 1);
        new_arg_flags[new_flag_pos++] = ctx->callsite->arg_flags[flag_pos];
    }

    /* now flatten any flattening hashes */
    for (arg_pos = 0; arg_pos < ctx->num_pos; arg_pos++) {

        arg_info.arg = ctx->args[arg_pos];
        arg_info.flags = ctx->callsite->arg_flags[arg_pos];

        if (!(arg_info.flags & MVM_CALLSITE_ARG_FLAT_NAMED))
            continue;

        if (arg_info.arg.o && REPR(arg_info.arg.o)->ID == MVM_REPR_ID_MVMHash) {
            MVMHashBody *body = &((MVMHash *)arg_info.arg.o)->body;
            MVMHashEntry *current, *tmp;

            HASH_ITER(hash_handle, body->hash_head, current, tmp) {

                if (new_arg_pos + 1 >= new_args_size) {
                    new_args = realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
                }
                if (new_flag_pos == new_arg_flags_size) {
                    new_arg_flags = realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
                }

                (new_args + new_arg_pos++)->s = (MVMString *)current->key;
                (new_args + new_arg_pos++)->o = current->value;
                new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_NAMED | MVM_CALLSITE_ARG_OBJ;
            }
        }
        else if (arg_info.arg.o) {
            MVM_exception_throw_adhoc(tc, "flattening of other hash reprs NYI.");
        }
    }

    init_named_used(tc, ctx, (new_arg_pos - new_num_pos) / 2);
    ctx->args = new_args;
    ctx->arg_count = new_arg_pos;
    ctx->num_pos = new_num_pos;
    ctx->arg_flags = new_arg_flags;
}
