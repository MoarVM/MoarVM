/* Representation for an exception in MoarVM. */
struct MVMExceptionBody {
    /* The exception message. */
    MVMString *message;

    /* The payload (object thrown with the exception). */
    MVMObject *payload;

    /* The exception category. */
    MVMint32 category;

    /* Where was the exception thrown from? */
    MVMFrame *origin;

    /* Where should we resume to, if it's possible? */
    MVMuint8 *resume_addr;
};
struct MVMException {
    MVMObject common;
    MVMExceptionBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMException_initialize(MVMThreadContext *tc);
