#include "moarvm.h"

/* Struct used internally in here. */
struct MVMArgInfo {
    MVMRegister      *arg;
    MVMCallsiteEntry  flags;
};

/* Initialize arguments processing context. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args) {
    /* Stash callsite and argument count. */
    ctx->callsite = callsite;
    ctx->args     = args;
}

/* Clean up an arguments processing context. */
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    /* Currently nothing to do. */
}

static const char * get_arg_type_name(MVMThreadContext *tc, MVMuint8 type) {
    if (type & MVM_CALLSITE_ARG_OBJ)  return "object";
    if (type & MVM_CALLSITE_ARG_INT)  return "integer";
    if (type & MVM_CALLSITE_ARG_UINT) return "unsigned integer";
    if (type & MVM_CALLSITE_ARG_NUM)  return "number";
    if (type & MVM_CALLSITE_ARG_STR)  return "string";
    MVM_exception_throw_adhoc(tc, "invalid arg type");
}

static const char * get_arg_type(MVMuint8 type) {
    if (type & MVM_CALLSITE_ARG_NAMED)  return "named";
    if (type & MVM_CALLSITE_ARG_FLAT)  return "flat";
    return "positional";
}


/* Checks that the passed arguments fall within the expected arity. */
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max) {
    MVMuint16 num_pos = ctx->callsite->num_pos;
    if (num_pos < min)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed %u, got %u", min, num_pos);
    if (num_pos > max)
        MVM_exception_throw_adhoc(tc, "Too many positional arguments; max %u, got %u", max, num_pos);
}

/* Get positional arguments. */
static struct MVMArgInfo find_pos_arg(MVMArgProcContext *ctx, MVMuint32 pos) {
    struct MVMArgInfo result;
    if (pos < ctx->callsite->num_pos) {
        result.arg = &ctx->args[pos];
        result.flags = ctx->callsite->arg_flags[pos];
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
MVMRegister * MVM_args_get_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough positional arguments; needed at least %u", pos + 1);
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_UINT))
        MVM_exception_throw_adhoc(tc, "Expected unsigned integer, got %s", get_arg_type_name(tc, result.flags));
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
    
    for (flag_pos = arg_pos = ctx->callsite->num_pos; arg_pos < ctx->callsite->arg_count; flag_pos++, arg_pos += 2) {
        if (MVM_string_equal(tc, ctx->args[arg_pos].s, name)) {
            result.arg = &ctx->args[arg_pos + 1];
            result.flags = ctx->callsite->arg_flags[flag_pos];
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
MVMRegister * MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Required named unsigned integer argument missing: %s", MVM_string_utf8_encode_C_string(tc, name));
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_UINT))
        MVM_exception_throw_adhoc(tc, "Expected unsigned integer for named argument %s, got %s",
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
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion to obj NYI; got type %u", target->return_type);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return an obj to");
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
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion to int NYI; got type %u", target->return_type);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return an int to");
    }
}
void MVM_args_set_result_uint(MVMThreadContext *tc, MVMuint64 result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            case MVM_RETURN_UINT:
                target->return_value->ui64 = result;
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion to uint NYI; got type %u", target->return_type);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a uint to");
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
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion to num NYI; got type %u", target->return_type);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a num to");
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
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion to str NYI; got type %u", target->return_type);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a str to");
    }
}
void MVM_args_assert_void_return_ok(MVMThreadContext *tc, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target && target->return_type != MVM_RETURN_VOID)
        MVM_exception_throw_adhoc(tc, "Void return not allowed to context requiring a return value");
}
