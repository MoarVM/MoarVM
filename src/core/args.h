/* Callsite argument flags. */
typedef enum {
    /* Argument is an object. */
    MVM_CALLSITE_ARG_OBJ = 1,
    
    /* Argument is a native integer, signed. */
    MVM_CALLSITE_ARG_INT = 2,
    
    /* Argument is a native integer, unsigned. */
    MVM_CALLSITE_ARG_UINT = 4,
    
    /* Argument is a native floating point number. */
    MVM_CALLSITE_ARG_NUM = 8,
    
    /* Argument is a native NFG string (MVMString REPR). */
    MVM_CALLSITE_ARG_STR = 16,
    
    /* Argument is named; in this case, there are two entries in
     * the argument list, the first a MVMString naming the arg and
     * after that the arg. */
    MVM_CALLSITE_ARG_NAMED = 32,
    
    /* Argument is flattened. What this means is up to the target. */
    MVM_CALLSITE_ARG_FLAT = 64
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
    
    /* The total argument count. */
    MVMuint16 arg_count;
    
    /* Number of positionals (-1 indicates slurpy creating unknownness). */
    MVMint16 num_pos;
} MVMCallsite;

/* Argument processing context. */
typedef struct _MVMArgProcContext {
    /* The callsite we're processing. */
    MVMCallsite *callsite;
    
    /* The arguments. */
    union _MVMRegister *args;
} MVMArgProcContext;

/* Expected return type flags. */
typedef enum {
    /* Argument is an object. */
    MVM_RETURN_VOID = 0,
    
    /* Argument is an object. */
    MVM_RETURN_OBJ = 1,
    
    /* Argument is a native integer, signed. */
    MVM_RETURN_INT = 2,
    
    /* Argument is a native integer, unsigned. */
    MVM_RETURN_UINT = 4,
    
    /* Argument is a native floating point number. */
    MVM_RETURN_NUM = 8,
    
    /* Argument is a native NFG string (MVMString REPR). */
    MVM_RETURN_STR = 16,
} MVMReturnType;

/* Argument processing context handling. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, union _MVMRegister *args);
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx);
void MVM_args_checkarity(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint16 min, MVMuint16 max);

/* Argument access by position. */
union _MVMRegister * MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
union _MVMRegister * MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
union _MVMRegister * MVM_args_get_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
union _MVMRegister * MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
union _MVMRegister * MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);

/* Argument access by name. */
union _MVMRegister * MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
union _MVMRegister * MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
union _MVMRegister * MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
union _MVMRegister * MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
union _MVMRegister * MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);

/* Result setting. */
void MVM_args_set_result_obj(MVMThreadContext *tc, MVMObject *result, MVMint32 frameless);
void MVM_args_set_result_int(MVMThreadContext *tc, MVMint64 result, MVMint32 frameless);
void MVM_args_set_result_uint(MVMThreadContext *tc, MVMuint64 result, MVMint32 frameless);
void MVM_args_set_result_num(MVMThreadContext *tc, MVMnum64 result, MVMint32 frameless);
void MVM_args_set_result_str(MVMThreadContext *tc, struct _MVMString *result, MVMint32 frameless);

/* Result setting frame constants. */
#define MVM_RETURN_CALLER_FRAME     0
#define MVM_RETURN_CURRENT_FRAME    1

/* Required/optional constants. */
#define MVM_ARG_OPTIONAL    0
#define MVM_ARG_REQUIRED    1
