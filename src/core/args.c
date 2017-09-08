#include "moar.h"

MVM_STATIC_INLINE MVMint32 is_named_used(MVMArgProcContext *ctx, MVMuint32 idx) {
    return ctx->named_used_size > 64
        ? ctx->named_used.byte_array[idx]
        : ctx->named_used.bit_field & ((MVMuint64)1 << idx);
}

MVM_STATIC_INLINE void mark_named_used(MVMArgProcContext *ctx, MVMuint32 idx) {
    if (ctx->named_used_size > 64)
        ctx->named_used.byte_array[idx] = 1;
    else
        ctx->named_used.bit_field |= (MVMuint64)1 << idx;
}

/* Marks a named used in the current callframe. */
void MVM_args_marked_named_used(MVMThreadContext *tc, MVMuint32 idx) {
    mark_named_used(&(tc->cur_frame->params), idx);
}

static void init_named_used(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 num) {
    ctx->named_used_size = num;
    if (num > 64)
        ctx->named_used.byte_array = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, num); 
    else
        ctx->named_used.bit_field = 0;
}

/* Initialize arguments processing context. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args) {
    /* Stash callsite and argument counts/pointers. */
    ctx->callsite = callsite;
    /* initial counts and values; can be altered by flatteners */
    init_named_used(tc, ctx, MVM_callsite_num_nameds(tc, callsite));
    ctx->args     = args;
    ctx->num_pos  = callsite->num_pos;
    ctx->arg_count = callsite->arg_count;
    ctx->arg_flags = NULL; /* will be populated by flattener if needed */
}

/* Clean up an arguments processing context. */
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    if (ctx->arg_flags) {
        MVM_free(ctx->arg_flags);
        MVM_free(ctx->args);
    }
    if (ctx->named_used_size > 64) {
        MVM_fixed_size_free(tc, tc->instance->fsa, ctx->named_used_size,
            ctx->named_used.byte_array);
        ctx->named_used_size = 0;
    }
}

/* Make a copy of the callsite. */
MVMCallsite * MVM_args_copy_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMCallsite      *res   = MVM_calloc(1, sizeof(MVMCallsite));
    MVMCallsiteEntry *flags = NULL;
    MVMCallsiteEntry *src_flags;
    MVMint32 fsize;

    if (ctx->arg_flags) {
        fsize = ctx->flag_count;
        src_flags = ctx->arg_flags;
    }
    else {
        fsize = ctx->callsite->flag_count;
        src_flags = ctx->callsite->arg_flags;
    }

    if (fsize) {
        flags = MVM_malloc(fsize * sizeof(MVMCallsiteEntry));
        memcpy(flags, src_flags, fsize * sizeof(MVMCallsiteEntry));
    }
    res->flag_count = fsize;
    res->arg_flags = flags;
    res->arg_count = ctx->arg_count;
    res->num_pos   = ctx->num_pos;
    return res;
}

/* Copy a callsite unless it is interned. */
MVMCallsite * MVM_args_copy_uninterned_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    return ctx->callsite->is_interned && !ctx->arg_flags
        ? ctx->callsite
        : MVM_args_copy_callsite(tc, ctx);
}

MVMObject * MVM_args_use_capture(MVMThreadContext *tc, MVMFrame *f) {
    /* We used to try and avoid some GC churn by keeping one call capture per
     * thread that was mutated. However, its lifetime was difficult to manage,
     * leading to leaks and subtle bugs. So, we use save_capture always now
     * for this; we may later eliminate it using escape analysis, or treat
     * it differently in the optimizer. */
    return MVM_args_save_capture(tc, f);
}

MVMObject * MVM_args_save_capture(MVMThreadContext *tc, MVMFrame *frame) {
    MVMObject *cc_obj;
    MVMROOT(tc, frame, {
        MVMCallCapture *cc = (MVMCallCapture *)
            (cc_obj = MVM_repr_alloc_init(tc, tc->instance->CallCapture));

        /* Copy the arguments. */
        MVMuint32 arg_size = frame->params.arg_count * sizeof(MVMRegister);
        MVMRegister *args = MVM_malloc(arg_size);
        memcpy(args, frame->params.args, arg_size);

        /* Set up the call capture, copying the callsite. */
        cc->body.apc  = (MVMArgProcContext *)MVM_calloc(1, sizeof(MVMArgProcContext));
        MVM_args_proc_init(tc, cc->body.apc,
            MVM_args_copy_uninterned_callsite(tc, &frame->params),
            args);
    });
    return cc_obj;
}

static void flatten_args(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Checks that the passed arguments fall within the expected arity. */
static void arity_fail(MVMThreadContext *tc, MVMuint16 got, MVMuint16 min, MVMuint16 max) {
    char *problem = got > max ? "Too many" : "Too few";
    if (min == max)
        MVM_exception_throw_adhoc(tc, "%s positionals passed; expected %d argument%s but got %d",
            problem, min, (min == 1 ? "" : "s"), got);
    else if (max == 0xFFFF)
        MVM_exception_throw_adhoc(tc, "%s positionals passed; expected at least %d arguments but got only %d",
            problem, min, got);
    else
        MVM_exception_throw_adhoc(tc, "%s positionals passed; expected %d %s %d arguments but got %d",
            problem, min, (min + 1 == max ? "or" : "to"), max, got);
}
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max) {
    MVMuint16 num_pos;
    flatten_args(tc, ctx);
    num_pos = ctx->num_pos;
    if (num_pos < min || num_pos > max)
        arity_fail(tc, num_pos, min, max);
}

/* Get positional arguments. */
#define find_pos_arg(ctx, pos, result) do { \
    if (pos < ctx->num_pos) { \
        result.arg   = ctx->args[pos]; \
        result.flags = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[pos]; \
        result.exists = 1; \
    } \
    else { \
        result.arg.s = NULL; \
        result.exists = 0; \
    } \
} while (0)

static MVMObject * decont_arg(MVMThreadContext *tc, MVMObject *arg) {
    MVMContainerSpec const *contspec = STABLE(arg)->container_spec;
    if (contspec) {
        if (contspec->fetch_never_invokes) {
            MVMRegister r;
            contspec->fetch(tc, arg, &r);
            return r.o;
        }
        else {
            MVM_exception_throw_adhoc(tc, "Cannot auto-decontainerize argument");
        }
    }
    else {
        return arg;
    }
}
#define autounbox(tc, type_flag, expected, result) do { \
    if (result.exists && !(result.flags & type_flag)) { \
        if (result.flags & MVM_CALLSITE_ARG_OBJ) { \
            MVMObject *obj = decont_arg(tc, result.arg.o); \
            switch (type_flag) { \
                case MVM_CALLSITE_ARG_INT: \
                    result.arg.i64 = MVM_repr_get_int(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_INT; \
                    break; \
                case MVM_CALLSITE_ARG_NUM: \
                    result.arg.n64 = MVM_repr_get_num(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_NUM; \
                    break; \
                case MVM_CALLSITE_ARG_STR: \
                    result.arg.s = MVM_repr_get_str(tc, obj); \
                    result.flags = MVM_CALLSITE_ARG_STR; \
                    break; \
                default: \
                    MVM_exception_throw_adhoc(tc, "Failed to unbox object to " expected); \
            } \
        } \
        if (!(result.flags & type_flag)) { \
            switch (type_flag) { \
                case MVM_CALLSITE_ARG_INT: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_NUM: \
                            MVM_exception_throw_adhoc(tc, "Expected native int argument, but got num"); \
                        case MVM_CALLSITE_ARG_STR: \
                            MVM_exception_throw_adhoc(tc, "Expected native int argument, but got str"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 1"); \
                    } \
                    break; \
                case MVM_CALLSITE_ARG_NUM: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_INT: \
                            MVM_exception_throw_adhoc(tc, "Expected native num argument, but got int"); \
                        case MVM_CALLSITE_ARG_STR: \
                            MVM_exception_throw_adhoc(tc, "Expected native num argument, but got str"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 2"); \
                    } \
                    break; \
                case MVM_CALLSITE_ARG_STR: \
                    switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
                        case MVM_CALLSITE_ARG_INT: \
                            MVM_exception_throw_adhoc(tc, "Expected native str argument, but got int"); \
                        case MVM_CALLSITE_ARG_NUM: \
                            MVM_exception_throw_adhoc(tc, "Expected native str argument, but got num"); \
                        default: \
                            MVM_exception_throw_adhoc(tc, "unreachable unbox 3"); \
                    } \
                    break; \
                default: \
                    MVM_exception_throw_adhoc(tc, "unreachable unbox 4"); \
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
    if (is_object) MVM_gc_root_temp_push(tc, (MVMCollectable **)&result); \
    box_type = target->static_info->body.cu->body.hll_config->box_type_obj; \
    box = REPR(box_type)->allocate(tc, STABLE(box_type)); \
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&box); \
    if (REPR(box)->initialize) \
        REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
    REPR(box)->box_funcs.set_func(tc, STABLE(box), box, OBJECT_BODY(box), result); \
    if (is_object) MVM_gc_root_temp_pop_n(tc, 2); \
    else MVM_gc_root_temp_pop(tc); \
    dest = box; \
} while (0)

#define autobox_int(tc, target, result, dest) do { \
    MVMObject *box, *box_type; \
    MVMint64 result_int = result; \
    box_type = target->static_info->body.cu->body.hll_config->int_box_type; \
    dest = MVM_intcache_get(tc, box_type, result_int); \
    if (!dest) { \
        box = REPR(box_type)->allocate(tc, STABLE(box_type)); \
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&box); \
        if (REPR(box)->initialize) \
            REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
        REPR(box)->box_funcs.set_int(tc, STABLE(box), box, OBJECT_BODY(box), result_int); \
        MVM_gc_root_temp_pop(tc); \
        dest = box; \
    } \
} while (0)

#define autobox_switch(tc, result) do { \
    if (result.exists) { \
        switch (result.flags & MVM_CALLSITE_ARG_MASK) { \
            case MVM_CALLSITE_ARG_OBJ: \
                break; \
            case MVM_CALLSITE_ARG_INT: \
                autobox_int(tc, tc->cur_frame, result.arg.i64, result.arg.o); \
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

MVMObject * MVM_args_get_required_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, MVM_ARG_REQUIRED, result);
    autobox_switch(tc, result);
    return result.arg.o;
}
MVMArgInfo MVM_args_get_optional_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, MVM_ARG_OPTIONAL, result);
    autobox_switch(tc, result);
    return result;
}
MVMint64 MVM_args_get_required_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, MVM_ARG_REQUIRED, result);
    autounbox(tc, MVM_CALLSITE_ARG_INT, "integer", result);
    return result.arg.i64;
}
MVMArgInfo MVM_args_get_optional_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, MVM_ARG_OPTIONAL, result);
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
MVMArgInfo MVM_args_get_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required) {
    MVMArgInfo result;
    args_get_pos(tc, ctx, pos, required, result);
    autounbox(tc, MVM_CALLSITE_ARG_INT, "unsigned integer", result);
    return result;
}

#define args_get_named(tc, ctx, name, required) do { \
     \
    MVMuint32 flag_pos, arg_pos; \
    result.arg.s = NULL; \
    result.exists = 0; \
     \
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2) { \
        if (MVM_string_equal(tc, ctx->args[arg_pos].s, name)) { \
            result.arg    = ctx->args[arg_pos + 1]; \
            result.flags  = (ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags)[flag_pos]; \
            result.exists = 1; \
            result.arg_idx = arg_pos + 1; \
            mark_named_used(ctx, (arg_pos - ctx->num_pos)/2); \
            break; \
        } \
    } \
    if (!result.exists && required) { \
        char *c_name = MVM_string_utf8_encode_C_string(tc, name); \
        char *waste[] = { c_name, NULL }; \
        MVM_exception_throw_adhoc_free(tc, waste, "Required named parameter '%s' not passed", c_name); \
    } \
} while (0)

MVMArgInfo MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required);
    autobox_switch(tc, result);
    return result;
}
MVMArgInfo MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required);
    autounbox(tc, MVM_CALLSITE_ARG_INT, "integer", result);
    return result;
}
MVMArgInfo MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required);
    autounbox(tc, MVM_CALLSITE_ARG_NUM, "number", result);
    return result;
}
MVMArgInfo MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required);
    autounbox(tc, MVM_CALLSITE_ARG_STR, "string", result);
    return result;
}
MVMArgInfo MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required) {
    MVMArgInfo result;
    args_get_named(tc, ctx, name, required);
    autounbox(tc, MVM_CALLSITE_ARG_INT, "unsigned integer", result);
    return result;
}
MVMint64 MVM_args_has_named(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name) {
    MVMuint32 flag_pos, arg_pos;
    for (flag_pos = arg_pos = ctx->num_pos; arg_pos < ctx->arg_count; flag_pos++, arg_pos += 2)
        if (MVM_string_equal(tc, ctx->args[arg_pos].s, name))
            return 1;
    return 0;
}
void MVM_args_assert_nameds_used(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMuint16 size = ctx->named_used_size;
    MVMuint16 i;
    if (size > 64) {
        for (i = 0; i < size; i++)
            if (!ctx->named_used.byte_array[i]) {
                char *c_param = MVM_string_utf8_encode_C_string(tc,
                    ctx->args[ctx->num_pos + 2 * i].s);
                char *waste[] = { c_param, NULL };
                MVM_exception_throw_adhoc_free(tc, waste,
                    "Unexpected named argument '%s' passed",
                    c_param);
            }
    }
    else {
        for (i = 0; i < size; i++)
            if (!(ctx->named_used.bit_field & ((MVMuint64)1 << i))) {
                char *c_param = MVM_string_utf8_encode_C_string(tc,
                    ctx->args[ctx->num_pos + 2 * i].s);
                char *waste[] = { c_param, NULL };
                MVM_exception_throw_adhoc_free(tc, waste,
                    "Unexpected named argument '%s' passed",
                    c_param);
            }
    }
}

void MVM_args_throw_named_unused_error(MVMThreadContext *tc, MVMString *name) {
    char *c_param = MVM_string_utf8_encode_C_string(tc, name);
    char *waste[] = { c_param, NULL };
    MVM_exception_throw_adhoc_free(tc, waste,
        "Unexpected named argument '%s' passed",
        c_param);
}

/* Result setting. The frameless flag indicates that the currently
 * executing code does not have a MVMFrame of its own. */
static MVMObject * decont_result(MVMThreadContext *tc, MVMObject *result) {
    MVMContainerSpec const *contspec = STABLE(result)->container_spec;
    if (contspec) {
        if (contspec->fetch_never_invokes) {
            MVMRegister r;
            contspec->fetch(tc, result, &r);
            return r.o;
        }
        else {
            MVM_exception_throw_adhoc(tc, "Cannot auto-decontainerize return value");
        }
    }
    else {
        return result;
    }
}
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
                target->return_value->i64 = MVM_repr_get_int(tc, decont_result(tc, result));
                break;
            case MVM_RETURN_NUM:
                target->return_value->n64 = MVM_repr_get_num(tc, decont_result(tc, result));
                break;
            case MVM_RETURN_STR:
                target->return_value->s = MVM_repr_get_str(tc, decont_result(tc, result));
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
            case MVM_RETURN_OBJ:
                autobox(tc, target, result, int_box_type, 0, set_int, target->return_value->o);
                break;
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
            case MVM_RETURN_OBJ:
                autobox(tc, target, result, num_box_type, 0, set_num, target->return_value->o);
                break;
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
            case MVM_RETURN_OBJ:
                autobox(tc, target, result, str_box_type, 1, set_str, target->return_value->o);
                break;
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

#define box_slurpy_pos(tc, type, result, box, value, reg, box_type_obj, name, set_func) do { \
    type = (*(tc->interp_cu))->body.hll_config->box_type_obj; \
    if (!type || IS_CONCRETE(type)) { \
        MVM_exception_throw_adhoc(tc, "Missing hll " name " box type"); \
    } \
    box = REPR(type)->allocate(tc, STABLE(type)); \
    if (REPR(box)->initialize) \
        REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
    REPR(box)->box_funcs.set_func(tc, STABLE(box), box, \
        OBJECT_BODY(box), value); \
    reg.o = box; \
    REPR(result)->pos_funcs.push(tc, STABLE(result), result, \
        OBJECT_BODY(result), reg, MVM_reg_obj); \
} while (0)

#define box_slurpy_pos_int(tc, type, result, box, value, reg, box_type_obj, name, set_func) do { \
    type = (*(tc->interp_cu))->body.hll_config->box_type_obj; \
    if (!type || IS_CONCRETE(type)) { \
        MVM_exception_throw_adhoc(tc, "Missing hll " name " box type"); \
    } \
    box = MVM_intcache_get(tc, type, value); \
    if (!box) { \
        box = REPR(type)->allocate(tc, STABLE(type)); \
        if (REPR(box)->initialize) \
            REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
        REPR(box)->box_funcs.set_func(tc, STABLE(box), box, \
            OBJECT_BODY(box), value); \
    } \
    reg.o = box; \
    REPR(result)->pos_funcs.push(tc, STABLE(result), result, \
        OBJECT_BODY(result), reg, MVM_reg_obj); \
} while (0)

MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos) {
    MVMObject *type = (*(tc->interp_cu))->body.hll_config->slurpy_array_type, *result = NULL, *box = NULL;
    MVMArgInfo arg_info;
    MVMRegister reg;

    if (!type || IS_CONCRETE(type)) {
        MVM_exception_throw_adhoc(tc, "Missing hll slurpy array type");
    }

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&type);
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
                box_slurpy_pos_int(tc, type, result, box, arg_info.arg.i64, reg, int_box_type, "int", set_int);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                box_slurpy_pos(tc, type, result, box, arg_info.arg.n64, reg, num_box_type, "num", set_num);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&arg_info.arg.s);
                box_slurpy_pos(tc, type, result, box, arg_info.arg.s, reg, str_box_type, "str", set_str);
                MVM_gc_root_temp_pop(tc);
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "arg flag is empty in slurpy positional");
        }

        find_pos_arg(ctx, pos, arg_info);
        pos++;
        if (pos == 1) break; /* overflow?! */
    }

    MVM_gc_root_temp_pop_n(tc, 3);

    return result;
}

#define box_slurpy_named(tc, type, result, box, value, reg, box_type_obj, name, set_func, key) do { \
    type = (*(tc->interp_cu))->body.hll_config->box_type_obj; \
    if (!type || IS_CONCRETE(type)) { \
        MVM_exception_throw_adhoc(tc, "Missing hll " name " box type"); \
    } \
    box = REPR(type)->allocate(tc, STABLE(type)); \
    if (REPR(box)->initialize) \
        REPR(box)->initialize(tc, STABLE(box), box, OBJECT_BODY(box)); \
    REPR(box)->box_funcs.set_func(tc, STABLE(box), box, \
        OBJECT_BODY(box), value); \
    reg.o = box; \
    REPR(result)->ass_funcs.bind_key(tc, STABLE(result), result, \
        OBJECT_BODY(result), (MVMObject *)key, reg, MVM_reg_obj); \
} while (0)

MVMObject * MVM_args_slurpy_named(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMObject *type = (*(tc->interp_cu))->body.hll_config->slurpy_hash_type, *result = NULL, *box = NULL;
    MVMArgInfo arg_info;
    MVMuint32 flag_pos, arg_pos;
    MVMRegister reg;
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

        if (is_named_used(ctx, flag_pos - ctx->num_pos))
            continue;

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
                REPR(result)->ass_funcs.bind_key(tc, STABLE(result),
                    result, OBJECT_BODY(result), (MVMObject *)key, arg_info.arg, MVM_reg_obj);
                break;
            }
            case MVM_CALLSITE_ARG_INT: {
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);
                box_slurpy_named(tc, type, result, box, arg_info.arg.i64, reg, int_box_type, "int", set_int, key);
                MVM_gc_root_temp_pop(tc);
                break;
            }
            case MVM_CALLSITE_ARG_NUM: {
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);
                box_slurpy_named(tc, type, result, box, arg_info.arg.n64, reg, num_box_type, "num", set_num, key);
                MVM_gc_root_temp_pop(tc);
                break;
            }
            case MVM_CALLSITE_ARG_STR: {
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&key);
                MVM_gc_root_temp_push(tc, (MVMCollectable **)&arg_info.arg.s);
                box_slurpy_named(tc, type, result, box, arg_info.arg.s, reg, str_box_type, "str", set_str, key);
                MVM_gc_root_temp_pop_n(tc, 2);
                break;
            }
            default:
                MVM_exception_throw_adhoc(tc, "arg flag is empty in slurpy named");
        }
    }

    MVM_gc_root_temp_pop_n(tc, 2);

    return result;
}

static MVMint32 seen_name(MVMThreadContext *tc, MVMString *name, MVMRegister *new_args, MVMint32 first_named, MVMint32 num_new_args) {
    MVMint32 j;
    for (j = first_named; j < num_new_args; j += 2)
        if (MVM_string_equal(tc, new_args[j].s, name))
            return 1;
    return 0;
}
static void flatten_args(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    MVMArgInfo arg_info;
    MVMint32 flag_pos = 0, arg_pos = 0, new_arg_pos = 0,
        new_arg_flags_size = ctx->arg_count > 0x7FFF ? ctx->arg_count : ctx->arg_count * 2,
        new_args_size = new_arg_flags_size, i, new_flag_pos = 0, new_num_pos = 0;
    MVMCallsiteEntry *new_arg_flags;
    MVMRegister *new_args;

    if (!ctx->callsite->has_flattening) return;

    new_arg_flags = MVM_malloc(new_arg_flags_size * sizeof(MVMCallsiteEntry));
    new_args = MVM_malloc(new_args_size * sizeof(MVMRegister));

    /* First flatten any positionals in amongst any non-flattening
     * positionals. */
    for ( ; arg_pos < ctx->num_pos; arg_pos++) {

        arg_info.arg    = ctx->args[arg_pos];
        arg_info.flags  = ctx->callsite->arg_flags[arg_pos];
        arg_info.exists = 1;

        /* Skip it if it's not flattening or is null. The bytecode loader
         * verifies it's a MVM_CALLSITE_ARG_OBJ. */
        if ((arg_info.flags & MVM_CALLSITE_ARG_FLAT) && arg_info.arg.o) {
            MVMObject      *list  = arg_info.arg.o;
            MVMint64        count = REPR(list)->elems(tc, STABLE(list), list, OBJECT_BODY(list));
            MVMStorageSpec  lss   = REPR(list)->pos_funcs.get_elem_storage_spec(tc, STABLE(list));

            if ((MVMint64)new_arg_pos + count > 0xFFFF) {
                MVM_exception_throw_adhoc(tc, "Too many arguments in flattening array.");
            }

            for (i = 0; i < count; i++) {
                if (new_arg_pos == new_args_size) {
                    new_args = MVM_realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
                }
                if (new_flag_pos == new_arg_flags_size) {
                    new_arg_flags = MVM_realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
                }

                switch (lss.inlineable ? lss.boxed_primitive : 0) {
                    case MVM_STORAGE_SPEC_BP_INT:
                        (new_args + new_arg_pos++)->i64 = MVM_repr_at_pos_i(tc, list, i);
                        new_arg_flags[new_flag_pos++]   = MVM_CALLSITE_ARG_INT;
                        break;
                    case MVM_STORAGE_SPEC_BP_NUM:
                        (new_args + new_arg_pos++)->n64 = MVM_repr_at_pos_n(tc, list, i);
                        new_arg_flags[new_flag_pos++]   = MVM_CALLSITE_ARG_NUM;
                        break;
                    case MVM_STORAGE_SPEC_BP_STR:
                        (new_args + new_arg_pos++)->s = MVM_repr_at_pos_s(tc, list, i);
                        new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_STR;
                        break;
                    default:
                        (new_args + new_arg_pos++)->o = MVM_repr_at_pos_o(tc, list, i);
                        new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_OBJ;
                        break;
                }
            }
        }
        else {
            if (new_arg_pos == new_args_size) {
                new_args = MVM_realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
            }
            if (new_flag_pos == new_arg_flags_size) {
                new_arg_flags = MVM_realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
            }

            *(new_args + new_arg_pos++) = arg_info.arg;
            new_arg_flags[new_flag_pos++] = arg_info.flags;
        }
    }
    new_num_pos = new_arg_pos;

    /* Then flatten in any nameds, amongst non-flattening nameds, starting
     * from the right and skipping duplicates. */
    flag_pos = ctx->callsite->flag_count;
    arg_pos = ctx->arg_count;
    while (flag_pos > ctx->num_pos) {
        flag_pos--;
        if (ctx->callsite->arg_flags[flag_pos] & MVM_CALLSITE_ARG_FLAT_NAMED) {
            arg_info.flags = ctx->callsite->arg_flags[flag_pos];
            arg_pos--;
            arg_info.arg = ctx->args[arg_pos];

            if (arg_info.arg.o && REPR(arg_info.arg.o)->ID == MVM_REPR_ID_MVMHash) {
                MVMHashBody *body = &((MVMHash *)arg_info.arg.o)->body;
                MVMHashEntry *current, *tmp;
                unsigned bucket_tmp;

                HASH_ITER(hash_handle, body->hash_head, current, tmp, bucket_tmp) {
                    MVMString *arg_name = MVM_HASH_KEY(current);
                    if (!seen_name(tc, arg_name, new_args, new_num_pos, new_arg_pos)) {
                        if (new_arg_pos + 1 >= new_args_size) {
                            new_args = MVM_realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
                        }
                        if (new_flag_pos == new_arg_flags_size) {
                            new_arg_flags = MVM_realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
                        }

                        (new_args + new_arg_pos++)->s = arg_name;
                        (new_args + new_arg_pos++)->o = current->value;
                        new_arg_flags[new_flag_pos++] = MVM_CALLSITE_ARG_NAMED | MVM_CALLSITE_ARG_OBJ;
                    }
                }
            }
            else if (arg_info.arg.o) {
                MVM_exception_throw_adhoc(tc, "flattening of other hash reprs NYI.");
            }
        }
        else {
            arg_pos -= 2;
            if (!seen_name(tc, (ctx->args + arg_pos)->s, new_args, new_num_pos, new_arg_pos)) {
                if (new_arg_pos + 1 >= new_args_size) {
                    new_args = MVM_realloc(new_args, (new_args_size *= 2) * sizeof(MVMRegister));
                }
                if (new_flag_pos == new_arg_flags_size) {
                    new_arg_flags = MVM_realloc(new_arg_flags, (new_arg_flags_size *= 2) * sizeof(MVMCallsiteEntry));
                }

                (new_args + new_arg_pos++)->s = (ctx->args + arg_pos)->s;
                *(new_args + new_arg_pos++) = *(ctx->args + arg_pos + 1);
                new_arg_flags[new_flag_pos++] = ctx->callsite->arg_flags[flag_pos];
            }
        }
    }

    if (ctx->named_used_size > 64)
        MVM_fixed_size_free(tc, tc->instance->fsa, ctx->named_used_size, ctx->named_used.byte_array);
    init_named_used(tc, ctx, (new_arg_pos - new_num_pos) / 2);
    ctx->args = new_args;
    ctx->arg_count = new_arg_pos;
    ctx->num_pos = new_num_pos;
    ctx->arg_flags = new_arg_flags;
    ctx->flag_count = new_flag_pos;
}

/* Does the common setup work when we jump the interpreter into a chosen
 * call from C-land. */
void MVM_args_setup_thunk(MVMThreadContext *tc, MVMRegister *res_reg, MVMReturnType return_type, MVMCallsite *callsite) {
    MVMFrame *cur_frame          = tc->cur_frame;
    cur_frame->return_value      = res_reg;
    cur_frame->return_type       = return_type;
    cur_frame->return_address    = *(tc->interp_cur_op);
    cur_frame->cur_args_callsite = callsite;
}

/* Custom bind failure handling. Invokes the HLL's bind failure handler, with
 * an argument capture */
static void bind_error_return(MVMThreadContext *tc, void *sr_data) {
    MVMRegister *r   = (MVMRegister *)sr_data;
    MVMObject   *res = r->o;
    MVM_free(r);
    if (tc->cur_frame->caller)
        MVM_args_set_result_obj(tc, res, 0);
    else
        MVM_exception_throw_adhoc(tc, "No caller to return to after bind_error");
    MVM_frame_try_return(tc);
}
static void mark_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    MVMRegister *r = (MVMRegister *)frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &r->o);
}
void MVM_args_bind_failed(MVMThreadContext *tc) {
    MVMRegister *res;
    MVMCallsite *inv_arg_callsite;

    /* Capture arguments into a call capture, to pass off for analysis. */
    MVMObject *cc_obj = MVM_args_save_capture(tc, tc->cur_frame);

    /* Invoke the HLL's bind failure handler. */
    MVMFrame *cur_frame = tc->cur_frame;
    MVMObject *bind_error = MVM_hll_current(tc)->bind_error;
    if (!bind_error)
        MVM_exception_throw_adhoc(tc, "Bind error occurred, but HLL has no handler");
    bind_error = MVM_frame_find_invokee(tc, bind_error, NULL);
    res = MVM_calloc(1, sizeof(MVMRegister));
    inv_arg_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    MVM_args_setup_thunk(tc, res, MVM_RETURN_OBJ, inv_arg_callsite);
    MVM_frame_special_return(tc, cur_frame, bind_error_return, NULL, res, mark_sr_data);
    cur_frame->args[0].o = cc_obj;
    STABLE(bind_error)->invoke(tc, bind_error, inv_arg_callsite, cur_frame->args);
}
