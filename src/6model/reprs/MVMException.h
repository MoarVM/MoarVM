/* Representation for an exception in MoarVM. */
struct MVMExceptionBody {
    /* The exception message. */
    MVMString *message;

    /* The payload (object thrown with the exception). */
    MVMObject *payload;

    /* The exception category. */
    MVMint32 category;

    /* Flag indicating if we should return after unwinding. */
    MVMuint8 return_after_unwind;

    /* Where was the exception thrown from? */
    MVMFrame *origin;
    MVMuint8 *throw_address;

    /* Where should we resume to, if it's possible? */
    MVMuint8 *resume_addr;
    void     *jit_resume_label;
};
struct MVMException {
    MVMObject common;
    MVMExceptionBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMException_initialize(MVMThreadContext *tc);
