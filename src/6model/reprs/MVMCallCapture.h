/* Representation for a context in the VM. Holds an MVMFrame. */
struct MVMCallCaptureBody {
    /* Argument processing context. For use mode, it points to the context of
     * the frame in question. For save mode, we allocate a fresh one. */
    MVMArgProcContext *apc;

    /* The effective MVMCallsite. This may be the original one, but in the
     * event of flattening will describe the flattened outcome. */
    MVMCallsite *effective_callsite;

    MVMuint8 owns_callsite;
};
struct MVMCallCapture {
    MVMObject common;
    MVMCallCaptureBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc);

MVMint64 MVM_capture_pos_primspec(MVMThreadContext *tc, MVMObject *capture, MVMint64 index);
