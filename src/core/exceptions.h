/* Exception handler actions. */
#define MVM_EX_ACTION_GOTO       0
#define MVM_EX_ACTION_GOTO_OBJ   1
#define MVM_EX_ACTION_INVOKE     2

/* Exception categories. */
#define MVM_EX_CAT_CATCH         1
#define MVM_EX_CAT_CONTROL       2
#define MVM_EX_CAT_NEXT          4
#define MVM_EX_CAT_REDO          8
#define MVM_EX_CAT_LAST          16
#define MVM_EX_CAT_RETURN        32
#define MVM_EX_CAT_UNWIND        64
#define MVM_EX_CAT_TAKE          128
#define MVM_EX_CAT_WARN          256
#define MVM_EX_CAT_SUCCEED       512
#define MVM_EX_CAT_PROCEED       1024

/* Ways to throw an exception. */
#define MVM_EX_THROW_DYN         0
#define MVM_EX_THROW_LEX         1
#define MVM_EX_THROW_LEXOTIC     2

/* Information associated with an exception handler. */
typedef struct _MVMFrameHandler {
    /* Start offset into the frame's bytecode for the handler, inclusive. */
    MVMuint32 start_offset;

    /* End offset into the frame's bytecode for the handler, exclusive. */
    MVMuint32 end_offset;

    /* Category mask. */
    MVMuint32 category_mask;

    /* The kind of handler it is. */
    MVMuint16 action;

    /* Register containing block to invoke, for invokey handlers. */
    MVMuint16 block_reg;

    /* Offset into the frame's bytecode of the handler, for goto handlers. */
    MVMuint32 goto_offset;
} MVMFrameHandler;

/* An active (currently executing) exception handler. */
typedef struct _MVMActiveHandler {
    /* The frame the handler was found in. */
    struct _MVMFrame *frame;

    /* The handler information itself. */
    MVMFrameHandler *handler;

    /* The exception object. */
    MVMObject *ex_obj;

    /* The next active handler in the chain. */
    struct _MVMActiveHandler *next_handler;
} MVMActiveHandler;

/* Exception related functions. */
struct _MVMObject * MVM_exception_backtrace_strings(MVMThreadContext *tc, MVMObject *exObj);
void MVM_exception_throwcat(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, union _MVMRegister *resume_result);
void MVM_exception_throwobj(MVMThreadContext *tc, MVMuint8 mode, MVMObject *exObj, MVMRegister *resume_result);
struct _MVMObject * MVM_exception_newlexotic(MVMThreadContext *tc, MVMuint32 offset);
void MVM_exception_gotolexotic(MVMThreadContext *tc, MVMFrameHandler *h, struct _MVMFrame *f);
MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_GCC;
MVM_NO_RETURN void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) MVM_NO_RETURN_GCC;

/* Exit codes for panic. */
#define MVM_exitcode_NYI            12
#define MVM_exitcode_compunit       13
#define MVM_exitcode_invalidopcode  14
#define MVM_exitcode_gcalloc        15
#define MVM_exitcode_gcroots        16
#define MVM_exitcode_gcnursery      17
#define MVM_exitcode_threads        18
#define MVM_exitcode_gcorch         19
