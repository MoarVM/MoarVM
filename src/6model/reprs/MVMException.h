/* Representation for an exception in MoarVM. */
struct MVMExceptionBody {
    /* The exception message. */
    MVMString *message;

    /* The payload (object thrown with the exception). */
    MVMObject *payload;

    /* The exception category. */
    MVMint32 category;

    /* Is the exception resumable? */
    MVMuint8 resumable;

    /* Where was the exception thrown from? */
    MVMFrame *origin;
};
struct MVMException {
    MVMObject common;
    MVMExceptionBody body;
};

/* Function for REPR setup. */
MVMREPROps * MVMException_initialize(MVMThreadContext *tc);
