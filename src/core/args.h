/* Argument processing context. */
struct MVMArgProcContext {
    /* The callsite we're processing. */
    MVMCallsite *callsite;

    /* The set of flags (only set if we flattened, otherwise we use the ones
     * from callsite). */
    MVMCallsiteEntry *arg_flags;

    /* The arguments. */
    MVMRegister *args;

    /* Indexes of used nameds. If named_used_size is less than or equal to
     * 64, it will be a bit field. Otherwise, it will be a pointer to a
     * byte array. */
    union {
        MVMuint8 *byte_array;
        MVMuint64 bit_field;
    } named_used;
    MVMuint16 named_used_size;

    /* The total argument count (including 2 for each
     * named arg). */
    MVMuint16 arg_count;

    /* Number of positionals. */
    MVMuint16 num_pos;

    /* The number of arg flags; only valid if arg_flags isn't NULL. */
    MVMuint16 flag_count;
};

/* Expected return type flags. */
typedef MVMuint8 MVMReturnType;
#define MVM_RETURN_VOID     0
#define MVM_RETURN_OBJ      1
#define MVM_RETURN_INT      2
#define MVM_RETURN_NUM      4
#define MVM_RETURN_STR      8

/* Struct used for returning information about an argument. */
struct MVMArgInfo {
    MVMRegister arg;
    MVMCallsiteEntry   flags;
    MVMuint8           exists;
    MVMuint16          arg_idx; /* Set only for nameds, obvious for pos */
};

/* Argument processing context handling. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args);
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max);
void MVM_args_checkarity_for_jit(MVMThreadContext *tc, MVMuint16 min, MVMuint16 max);
MVMCallsite * MVM_args_copy_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVMCallsite * MVM_args_copy_uninterned_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVM_PUBLIC MVMObject * MVM_args_use_capture(MVMThreadContext *tc, MVMFrame *f);
MVM_PUBLIC MVMObject * MVM_args_save_capture(MVMThreadContext *tc, MVMFrame *f);
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
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless);
void MVM_args_set_result_str(MVMThreadContext *tc, MVMString *result, MVMint32 frameless);
void MVM_args_assert_void_return_ok(MVMThreadContext *tc, MVMint32 frameless);

/* Setting up calls from C-land. */
MVM_PUBLIC void MVM_args_setup_thunk(MVMThreadContext *tc, MVMRegister *return_value, MVMReturnType return_type,
    MVMCallsite *callsite);

/* Custom bind failure handling. */
void MVM_args_bind_failed(MVMThreadContext *tc);

/* Result setting frame constants. */
#define MVM_RETURN_CALLER_FRAME     0
#define MVM_RETURN_CURRENT_FRAME    1

/* Required/optional constants. */
#define MVM_ARG_OPTIONAL    0
#define MVM_ARG_REQUIRED    1
