/* Representation for a continuation in the VM. */
struct MVMContinuationBody {
    /* The top record on the callstack. */
    MVMCallStackRecord *stack_top;

    /* The first call stack region of continuation. */
    MVMCallStackRegion *first_region;

    /* Address to resume execution at when the continuation is invoked. */
    MVMuint8 *addr;

    /* Register to put invoke argument into after resume. */
    MVMRegister *res_reg;

    /* Active exception handler(s) to restore. */
    MVMActiveHandler *active_handlers;

    /* Flag to check we never invoke this continuation more than once (we rely
     * on continuations being one-shot, for example to know ->work can really
     * be cleared safely and that we'll never be running the same continuation
     * re-invocation on two threads at once). */
    AO_t invoked;

    /* If the continuationcontrol operation said to protect the tag, then we
     * keep it here so we can reinstate it. */
    MVMObject *protected_tag;

    /* If we're profiling, then data needed to cope with the continuation
     * being invoked again. */
    MVMProfileContinuationData *prof_cont;
};
struct MVMContinuation {
    MVMObject common;
    MVMContinuationBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMContinuation_initialize(MVMThreadContext *tc);
