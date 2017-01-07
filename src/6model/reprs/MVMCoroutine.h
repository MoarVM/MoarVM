/* Representation for a continuation in the VM. */
struct MVMCoroutineBody {
    MVMObject *code;

    /* Top frame of the continuation. */
    MVMFrame *top;

    /* Address to resume execution at when the continuation is invoked. */
    MVMuint8 *addr;

    /* Register to put invoke argument into after resume. */
    MVMRegister *res_reg;

    /* Root of the continuation. */
    MVMFrame *root;

    /* Active exception handler(s) to restore. */
    MVMActiveHandler *active_handlers;

    /* If we're profiling, then data needed to cope with the continuation
     * being invoked again. */
    MVMProfileContinuationData *prof_cont;
};
struct MVMCoroutine {
    MVMObject common;
    MVMCoroutineBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCoroutine_initialize(MVMThreadContext *tc);
