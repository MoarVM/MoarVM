/* Exception handler actions. */
#define MVM_EX_ACTION_GOTO                0
#define MVM_EX_ACTION_GOTO_WITH_PAYLOAD   1
#define MVM_EX_ACTION_INVOKE              2

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
#define MVM_EX_CAT_LABELED       4096
#define MVM_EX_CAT_AWAIT         8192
#define MVM_EX_CAT_EMIT          16384
#define MVM_EX_CAT_DONE          32768

/* Not a real category, but marks an inline boundary. */
#define MVM_EX_INLINE_BOUNDARY   2147483648

/* Ways to throw an exception. */
#define MVM_EX_THROW_DYN         0
#define MVM_EX_THROW_LEX         1
#define MVM_EX_THROW_LEXOTIC     2
#define MVM_EX_THROW_LEX_CALLER  3

/* Information associated with an exception handler. */
struct MVMFrameHandler {
    /* Start offset into the frame's bytecode for the handler, inclusive. */
    MVMuint32 start_offset;

    /* End offset into the frame's bytecode for the handler, exclusive. */
    MVMuint32 end_offset;

    /* Category mask or inline boundary indicator. */
    MVMuint32 category_mask;

    /* The kind of handler it is. */
    MVMuint16 action;

    /* Register containing block to invoke, for invokey handlers. */
    MVMuint16 block_reg;

    /* Offset into the frame's bytecode of the handler, for goto handlers. */
    MVMuint32 goto_offset;

    /* Register containing a label in case we have a labeled loop. We need to
     * be able to check for its identity when handling e.g. `next LABEL`. */
    MVMuint16 label_reg;

    /* The inlinee that this handler is associated with. Set to -1 for the
     * top-level handlers of a frame. Used both to skip non-top-level
     * handlers, but also to indicate, for a inline boundary indicator
     * entry in the table, the inline whose handlers end at this point. */
    MVMint16 inlinee;
};

/* An active (currently executing) exception handler. */
struct MVMActiveHandler {
    /* The frame the handler was found in. */
    MVMFrame *frame;

    /* The handler information itself. */
    MVMFrameHandler *handler;

    /* Handler information for a JITted handler */
    MVMJitHandler *jit_handler;

    /* The exception object. */
    MVMObject *ex_obj;

    /* The next active handler in the chain. */
    MVMActiveHandler *next_handler;
};

/* Exception related functions. */
MVMObject * MVM_exception_backtrace(MVMThreadContext *tc, MVMObject *ex_obj);
MVMObject * MVM_exception_backtrace_strings(MVMThreadContext *tc, MVMObject *exObj);
void MVM_dump_backtrace(MVMThreadContext *tc);
void MVM_exception_throwcat(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMRegister *resume_result);
void MVM_exception_die(MVMThreadContext *tc, MVMString *str, MVMRegister *rr);
void MVM_exception_throwobj(MVMThreadContext *tc, MVMuint8 mode, MVMObject *exObj, MVMRegister *resume_result);
void MVM_exception_throwpayload(MVMThreadContext *tc, MVMuint8 mode, MVMuint32 cat, MVMObject *payload, MVMRegister *resume_result);
void MVM_exception_resume(MVMThreadContext *tc, MVMObject *exObj);
MVM_PUBLIC MVM_NO_RETURN void MVM_panic_allocation_failed(size_t len) MVM_NO_RETURN_ATTRIBUTE;
MVM_PUBLIC MVM_NO_RETURN void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
MVM_PUBLIC MVM_NO_RETURN void MVM_oops(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
MVM_NO_RETURN void MVM_exception_throw_adhoc_va(MVMThreadContext *tc, const char *messageFormat, va_list args) MVM_NO_RETURN_ATTRIBUTE;
MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc_free(MVMThreadContext *tc, char **waste, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 3, 4);
MVM_NO_RETURN void MVM_exception_throw_adhoc_free_va(MVMThreadContext *tc, char **waste, const char *messageFormat, va_list args) MVM_NO_RETURN_ATTRIBUTE;
MVM_PUBLIC void MVM_crash_on_error(void);
char * MVM_exception_backtrace_line(MVMThreadContext *tc, MVMFrame *cur_frame, MVMuint16 not_top, MVMuint8 *throw_address);
MVMint32 MVM_get_exception_category(MVMThreadContext *tc, MVMObject *ex);
MVMObject * MVM_get_exception_payload(MVMThreadContext *tc, MVMObject *ex);
void MVM_bind_exception_payload(MVMThreadContext *tc, MVMObject *ex, MVMObject *payload);
void MVM_bind_exception_category(MVMThreadContext *tc, MVMObject *ex, MVMint32 category);
void MVM_exception_returnafterunwind(MVMThreadContext *tc, MVMObject *ex);

/* Exit codes for panic. */
#define MVM_exitcode_NYI            12
#define MVM_exitcode_compunit       13
#define MVM_exitcode_invalidopcode  14
#define MVM_exitcode_gcalloc        15
#define MVM_exitcode_gcroots        16
#define MVM_exitcode_gcnursery      17
#define MVM_exitcode_threads        18
#define MVM_exitcode_gcorch         19
