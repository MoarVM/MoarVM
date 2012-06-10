#include "moarvm.h"

/* Struct used internally in here. */
struct MVMArgInfo {
    MVMRegister      *arg;
    MVMCallsiteEntry  flags;
};

/* Initialize arguments processing context. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args) {
    MVMuint32 i;
    
    /* Stash callsite and argument count. */
    ctx->callsite = callsite;
    ctx->args     = args;
    
    /* Count positional arguments. */
    for (i = 0; i < callsite->arg_count; i++) {
        if (callsite->arg_flags[i] & MVM_CALLSITE_ARG_FLAT)
            MVM_exception_throw_adhoc(tc, "Flattening NYI");
        if (callsite->arg_flags[i] & MVM_CALLSITE_ARG_NAMED)
            break;
    }
    ctx->num_pos = i;
}

/* Clean up an arguments processing context. */
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    /* Currently nothing to do. */
}

/* Get positional arguments. */
static struct MVMArgInfo find_pos_arg(MVMArgProcContext *ctx, MVMuint32 pos) {
    struct MVMArgInfo result;
    if (pos < ctx->num_pos) {
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
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_OBJ))
        MVM_exception_throw_adhoc(tc, "Expected object");
    return result.arg;
}
MVMRegister * MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_INT))
        MVM_exception_throw_adhoc(tc, "Expected integer");
    return result.arg;
}
MVMRegister * MVM_args_get_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_UINT))
        MVM_exception_throw_adhoc(tc, "Expected unsigned integer");
    return result.arg;
}
MVMRegister * MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_NUM))
        MVM_exception_throw_adhoc(tc, "Expected number");
    return result.arg;
}
MVMRegister * MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    struct MVMArgInfo result = find_pos_arg(ctx, pos);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Expected string");
    return result.arg;
}

/* Get named arguments. */
static struct MVMArgInfo find_named_arg(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name) {
    struct MVMArgInfo result;
    MVMuint32 flag_pos, arg_pos;    
    result.arg = NULL;
    
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->callsite->arg_count; flag_pos++, arg_pos += 2) {
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
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_OBJ))
        MVM_exception_throw_adhoc(tc, "Expected object");
    return result.arg;
}
MVMRegister * MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_INT))
        MVM_exception_throw_adhoc(tc, "Expected integer");
    return result.arg;
}
MVMRegister * MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_UINT))
        MVM_exception_throw_adhoc(tc, "Expected unsigned integer");
    return result.arg;
}
MVMRegister * MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_NUM))
        MVM_exception_throw_adhoc(tc, "Expected number");
    return result.arg;
}
MVMRegister * MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    struct MVMArgInfo result = find_named_arg(tc, ctx, name);
    if (result.arg == NULL && required)
        MVM_exception_throw_adhoc(tc, "Not enough arguments");
    if (result.arg && !(result.flags & MVM_CALLSITE_ARG_STR))
        MVM_exception_throw_adhoc(tc, "Expected string");
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
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion NYI");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a result to");
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
                MVM_exception_throw_adhoc(tc, "Result return coercion NYI");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a result to");
    }
}
void MVM_args_set_result_uint(MVMThreadContext *tc, MVMuint64 result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion NYI");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a result to");
    }
}
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion NYI");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a result to");
    }
}
void MVM_args_set_result_str(MVMThreadContext *tc, MVMString *result, MVMint32 frameless) {
    MVMFrame *target = frameless ? tc->cur_frame : tc->cur_frame->caller;
    if (target) {
        switch (target->return_type) {
            case MVM_RETURN_VOID:
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Result return coercion NYI");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Could not locate frame to return a result to");
    }
}
