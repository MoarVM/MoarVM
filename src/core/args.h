/* Callsite argument flags. */
typedef enum {
    /* Argument is a native integer, signed. */
    MVM_CALLSITE_ARG_INT = 1,
    
    /* Argument is a native integer, unsigned. */
    MVM_CALLSITE_ARG_UINT = 2,
    
    /* Argument is a native floating point number. */
    MVM_CALLSITE_ARG_NUM = 4,
    
    /* Argument is a native NFG string (MVMString REPR). */
    MVM_CALLSITE_ARG_STR = 8,
    
    /* Argument is named; in this case, there are two entries in
     * the argument list, the first a MVMString naming the arg and
     * after that the arg. */
    MVM_CALLSITE_ARG_NAMED = 16,
    
    /* Argument is flattened. What this means is up to the target. */
    MVM_CALLSITE_ARG_FLAT = 32
} MVMCallsiteFlags;

/* A callsite entry is just one of the above flags. */
typedef MVMuint8 MVMCallsiteEntry;

/* A callsite is an argument count and a bunch of flags. Note that it
 * does not contain the values; this is the *statically known* things
 * about the callsite and is immutable. It describes how to process
 * the callsite memory buffer. */
typedef struct _MVMCallsite {
    MVMuint16 arg_count;
    MVMCallsiteEntry arg_flags[];
} MVMCallsite;

/* The arguments memory is an array of this union type; the static
 * callsite info serves as the discriminator. */
typedef union _MVMArg {
    MVMint64           i;
    MVMuint64          ui;
    MVMnum64           n;
    struct _MVMString *s;
    MVMObject         *o;
} MVMArg;

/* Argument processing context. */
typedef struct _MVMArgProcContext {
    /* The callsite we're processing. */
    MVMCallsite *callsite;
    
    /* The arguments. */
    MVMArg      *args;
} MVMArgProcContext;

/* Argument processing context handling. */
void MVM_args_proc_init(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMCallsite *callsite, MVMArg *args);
void MVM_args_proc_cleanup(MVMThreadContext *tc, MVMArgProcContext *ctx);

/* Argument access by position. */
MVMArg * MVM_args_get_pos_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArg * MVM_args_get_pos_int(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArg * MVM_args_get_pos_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArg * MVM_args_get_pos_num(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);
MVMArg * MVM_args_get_pos_str(MVMThreadContext *tc, MVMArgProcContext *ctx, MVMuint32 pos, MVMuint8 required);

/* Argument access by name. */
MVMArg * MVM_args_get_named_obj(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArg * MVM_args_get_named_int(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArg * MVM_args_get_named_uint(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArg * MVM_args_get_named_num(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);
MVMArg * MVM_args_get_named_str(MVMThreadContext *tc, MVMArgProcContext *ctx, struct _MVMString *name, MVMuint8 required);

/* Required/optional constants. */
#define MVM_ARG_OPTIONAL    0
#define MVM_ARG_REQUIRED    1
