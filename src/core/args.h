/* Container for arguments being passed to something to invoke it. */
struct MVMArgs {
    /* The callsite that describes the arguments. */
    MVMCallsite *callsite;

    /* A buffer we can obtain the arguments from. This is not a contiguous
     * list of arguments (at least, not in the common case!) In most cases,
     * it is the work area of the caller. */
    MVMRegister *source;

    /* A mapping array from argument number (according to flag index in the
     * callsite) to index in the source. Typically, this is a pointer into
     * the register list in the `dispatch` instruction. Thus to access arg 0
     * we'd do source[map[0]]. */
    MVMuint16 *map;
};

#define MVM_ARGS_SMALL_IDENTITY_MAP_SIZE    256
#define MVM_ARGS_LIMIT                      0xFFFF

/* Argument processing context. */
struct MVMArgProcContext {
    /* The incoming arguments to bind. */
    MVMArgs arg_info;

    /* Indexes of used nameds. If named_used_size is less than or equal to
     * 64, it will be a bit field. Otherwise, it will be a pointer to a
     * byte array. */
    union {
        MVMuint8 *byte_array;
        MVMuint64 bit_field;
    } named_used;
    MVMuint16 named_used_size;
};

/* Expected return type flags. */
typedef MVMuint8 MVMReturnType;
#define MVM_RETURN_VOID      0
#define MVM_RETURN_OBJ       1
#define MVM_RETURN_INT       2
#define MVM_RETURN_NUM       4
#define MVM_RETURN_STR       8
#define MVM_RETURN_UINT     32
/* When the thing returned to is "smart", like the debugserver,
 * set not only the return value but also the type depending on
 * which return_* op was used. */
#define MVM_RETURN_ALLOMORPH 16

/* Struct used for returning information about an argument. */
struct MVMArgInfo {
    MVMRegister arg;
    MVMCallsiteEntry   flags;
    MVMuint8           exists;
    MVMuint16          arg_idx; /* Set only for nameds, obvious for pos */
};

/* Initialize arguments processing context. */
MVM_STATIC_INLINE void MVM_args_proc_setup(MVMThreadContext *tc, MVMArgProcContext *ctx,
        MVMArgs arg_info) {
    ctx->arg_info = arg_info;
    MVMuint16 num_nameds = arg_info.callsite->flag_count - arg_info.callsite->num_pos;
    ctx->named_used_size = num_nameds;
    if (MVM_UNLIKELY(num_nameds > 64))
        ctx->named_used.byte_array = MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa,
                num_nameds);
    else
        ctx->named_used.bit_field = 0;
}

/* Clean up an arguments processing context. */
MVM_STATIC_INLINE void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx) {
    if (ctx->named_used_size > 64) {
        MVM_fixed_size_free(tc, tc->instance->fsa, ctx->named_used_size,
            ctx->named_used.byte_array);
        ctx->named_used_size = 0;
    }
}

/* Argument processing context handling. */
void MVM_args_setup_identity_map(MVMThreadContext *tc);
void MVM_args_grow_identity_map(MVMThreadContext *tc, MVMCallsite *callsite);
MVM_STATIC_INLINE MVMuint16 * MVM_args_identity_map(MVMThreadContext *tc,
        MVMCallsite *callsite) {
    if (callsite->flag_count > tc->instance->identity_arg_map_alloc)
        MVM_args_grow_identity_map(tc, callsite);
    return tc->instance->identity_arg_map;
}
void MVM_args_destroy_identity_map(MVMThreadContext *tc);
MVMCallStackFlattening * MVM_args_perform_flattening(MVMThreadContext *tc, MVMCallsite *cs,
        MVMRegister *source, MVMuint16 *map);

/* Arity checking and named handling. */
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max);
void MVM_args_checkarity_for_jit(MVMThreadContext *tc, MVMuint16 min, MVMuint16 max);
void MVM_args_marked_named_used(MVMThreadContext *tc, MVMuint32 idx);
void MVM_args_throw_named_unused_error(MVMThreadContext *tc, MVMString *name);

/* Argument access by position. */
MVMObject * MVM_args_get_required_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMArgInfo MVM_args_get_optional_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMint64 MVM_args_get_required_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMArgInfo MVM_args_get_optional_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMnum64 MVM_args_get_required_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMArgInfo MVM_args_get_optional_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMString * MVM_args_get_required_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMArgInfo MVM_args_get_optional_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMuint64 MVM_args_get_required_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMArgInfo MVM_args_get_optional_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos);
MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos);

/* Argument access by name. */
MVMArgInfo MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMObject * MVM_args_slurpy_named(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVMint64 MVM_args_has_named(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name);
void MVM_args_assert_nameds_used(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Result setting. */
void MVM_args_set_result_obj(MVMThreadContext *tc, MVMObject *result, MVMint32 frameless);
void MVM_args_set_result_int(MVMThreadContext *tc, MVMint64 result, MVMint32 frameless);
void MVM_args_set_result_uint(MVMThreadContext *tc, MVMuint64 result, MVMint32 frameless);
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless);
void MVM_args_set_result_str(MVMThreadContext *tc, MVMString *result, MVMint32 frameless);
void MVM_args_assert_void_return_ok(MVMThreadContext *tc, MVMint32 frameless);
void MVM_args_set_dispatch_result_obj(MVMThreadContext *tc, MVMFrame *target, MVMObject *result);
void MVM_args_set_dispatch_result_int(MVMThreadContext *tc, MVMFrame *target, MVMint64 result);
void MVM_args_set_dispatch_result_uint(MVMThreadContext *tc, MVMFrame *target, MVMuint64 result);
void MVM_args_set_dispatch_result_num(MVMThreadContext *tc, MVMFrame *target, MVMnum64 result);
void MVM_args_set_dispatch_result_str(MVMThreadContext *tc, MVMFrame *target, MVMString *result);

/* Capture handling. */
MVMCallsite * MVM_args_copy_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVM_PUBLIC MVMObject * MVM_args_use_capture(MVMThreadContext *tc, MVMFrame *f);
MVM_PUBLIC MVMObject * MVM_args_save_capture(MVMThreadContext *tc, MVMFrame *f);

/* Custom bind failure/success handling. */
void MVM_args_bind_failed(MVMThreadContext *tc, MVMDispInlineCacheEntry **ice_ptr);
void MVM_args_bind_succeeded(MVMThreadContext *tc, MVMDispInlineCacheEntry **ice_ptr);

/* Result setting frame constants. */
#define MVM_RETURN_CALLER_FRAME     0
#define MVM_RETURN_CURRENT_FRAME    1

/* Required/optional constants. */
#define MVM_ARG_OPTIONAL    0
#define MVM_ARG_REQUIRED    1
