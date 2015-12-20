/* Is this a capture that refers to the args area of an existing frame, or
 * a saved copy of a frame's args area? */
#define MVM_CALL_CAPTURE_MODE_USE   1
#define MVM_CALL_CAPTURE_MODE_SAVE  2

/* Representation for a context in the VM. Holds an MVMFrame. */
struct MVMCallCaptureBody {
    /* Argument processing context. For use mode, it points to the context of
     * the frame in question. For save mode, we allocate a fresh one. */
    MVMArgProcContext *apc;

    /* The frame the ArgProcContext lives in, if we're in use mode. This
     * ensures the frame stays alive long enough. */
    MVMFrame *use_mode_frame;

    /* The effective MVMCallsite. This may be the original one, but in the
     * event of flattening will describe the flattened outcome. */
    MVMCallsite *effective_callsite;

    /* Use or save mode? */
    MVMuint8 mode;

    MVMuint8 owns_callsite;
};
struct MVMCallCapture {
    MVMObject common;
    MVMCallCaptureBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc);
