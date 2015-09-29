/* Argument processing context. */
struct MVMArgProcContext {
    /* The callsite we're processing. */
    MVMCallsite *callsite;

    /* The set of flags. */
    MVMCallsiteEntry *arg_flags;

    /* The arguments. */
    MVMRegister *args;

    /* Bytemap of indexes of used nameds, so the
     * named slurpy knows which ones not to grab.
     * XXX cache and free this at the proper times. */
    MVMuint8 *named_used;
    MVMuint16 named_used_size;

    /* The total argument count (including 2 for each
     * named arg). */
    MVMuint16 arg_count;

    /* Number of positionals. */
    MVMuint16 num_pos;
};

/* Expected return type flags. */
typedef enum {
    /* Argument is an object. */
    MVM_RETURN_VOID = 0,

    /* Argument is an object. */
    MVM_RETURN_OBJ = 1,

    /* Argument is a native integer, signed. */
    MVM_RETURN_INT = 2,

    /* Argument is a native floating point number. */
    MVM_RETURN_NUM = 4,

    /* Argument is a native NFG string (MVMString REPR). */
    MVM_RETURN_STR = 8,
} MVMReturnType;

/* Struct used for returning information about an argument. */
struct MVMArgInfo {
    MVMRegister arg;
    MVMCallsiteEntry   flags;
    MVMuint8           exists;
};

/* Argument processing context handling. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMRegister *args);
void MVM_args_proc_cleanup_for_cache(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max);
void MVM_args_checkarity_for_jit(MVMThreadContext *tc, MVMuint16 min, MVMuint16 max);
MVMCallsite * MVM_args_copy_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVMCallsite * MVM_args_proc_to_callsite(MVMThreadContext *tc, MVMArgProcContext *ctx);
MVMCallsite * MVM_args_prepare(MVMThreadContext *tc, MVMCompUnit *cu, MVMint16 callsite_idx);
MVM_PUBLIC MVMObject * MVM_args_use_capture(MVMThreadContext *tc, MVMFrame *f);
MVM_PUBLIC MVMObject * MVM_args_save_capture(MVMThreadContext *tc, MVMFrame *f);

/* Argument access by position. */
MVMArgInfo MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos);

/* Argument access by name. */
MVMArgInfo MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMString *name, MVMuint8 required);
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
