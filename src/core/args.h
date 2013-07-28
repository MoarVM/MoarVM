/* Callsite argument flags. */
#define MVM_CALLSITE_ARG_MASK 31
typedef enum {
    /* Argument is an object. */
    MVM_CALLSITE_ARG_OBJ = 1,

    /* Argument is a native integer, signed. */
    MVM_CALLSITE_ARG_INT = 2,

    /* Argument is a native floating point number. */
    MVM_CALLSITE_ARG_NUM = 4,

    /* Argument is a native NFG string (MVMString REPR). */
    MVM_CALLSITE_ARG_STR = 8,

    /* Argument is named; in this case, there are two entries in
     * the argument list, the first a MVMString naming the arg and
     * after that the arg. */
    MVM_CALLSITE_ARG_NAMED = 32,

    /* Argument is flattened. What this means is up to the target. */
    MVM_CALLSITE_ARG_FLAT = 64,

    /* Argument is flattened and named. */
    MVM_CALLSITE_ARG_FLAT_NAMED = 128
} MVMCallsiteFlags;

/* A callsite entry is just one of the above flags. */
typedef MVMuint8 MVMCallsiteEntry;

/* A callsite is an argument count and a bunch of flags. Note that it
 * does not contain the values; this is the *statically known* things
 * about the callsite and is immutable. It describes how to process
 * the callsite memory buffer. */
typedef struct _MVMCallsite {
    /* The set of flags. */
    MVMCallsiteEntry *arg_flags;

    /* The total argument count (including 2 for each
     * named arg). */
    MVMuint16 arg_count;

    /* Number of positionals. */
    MVMuint16 num_pos;

    /* whether it has a flattening arg. */
    MVMuint8 has_flattening;
} MVMCallsite;

/* Argument processing context. */
/* adding these additional fields to MVMFrame adds only 12 bytes
 * (arg_flags, arg_count, and num_pos). */
typedef struct _MVMArgProcContext {
    /* The callsite we're processing. */
    MVMCallsite *callsite;

    /* The set of flags. */
    MVMCallsiteEntry *arg_flags;

    /* The arguments. */
    union _MVMRegister *args;

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
} MVMArgProcContext;

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
typedef struct _MVMArgInfo {
    union _MVMRegister arg;
    MVMCallsiteEntry   flags;
    MVMuint8           exists;
} MVMArgInfo;

/* Argument processing context handling. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, union _MVMRegister *args);
void MVM_args_proc_cleanup_for_cache(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max);

/* Argument access by position. */
MVMArgInfo MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArgInfo MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMObject * MVM_args_slurpy_positional(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 pos);

/* Argument access by name. */
MVMArgInfo MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArgInfo MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMObject * MVM_args_slurpy_named(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Result setting. */
void MVM_args_set_result_obj(MVMThreadContext *tc, MVMObject *result, MVMint32 frameless);
void MVM_args_set_result_int(MVMThreadContext *tc, MVMint64 result, MVMint32 frameless);
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless);
void MVM_args_set_result_str(MVMThreadContext *tc, struct _MVMString *result, MVMint32 frameless);
void MVM_args_assert_void_return_ok(MVMThreadContext *tc, MVMint32 frameless);

/* Result setting frame constants. */
#define MVM_RETURN_CALLER_FRAME     0
#define MVM_RETURN_CURRENT_FRAME    1

/* Required/optional constants. */
#define MVM_ARG_OPTIONAL    0
#define MVM_ARG_REQUIRED    1
