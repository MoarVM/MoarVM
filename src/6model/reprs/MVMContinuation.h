/* Representation for a continuation in the VM. */
struct MVMContinuationBody {
    /* Top frame of the continuation. */
    MVMFrame *top;
};
struct MVMContinuation {
    MVMObject common;
    MVMContinuationBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMContinuation_initialize(MVMThreadContext *tc);
