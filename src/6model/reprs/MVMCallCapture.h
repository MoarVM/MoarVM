/* Is this a capture that refers to the args area of an existing frame, or
 * a saved copy of a frame's args area? */
#define MVM_CALL_CAPTURE_MODE_USE   1
#define MVM_CALL_CAPTURE_MODE_SAVE  2

/* Representation for a context in the VM. Holds an MVMFrame. */
typedef struct _MVMCallCaptureBody {
    /* Argument processing context. For use mode, it points to the context of
     * the frame in question. For save mode, we allocate a fresh one. */
    MVMArgProcContext *apc;

    /* Use or save mode? */
    MVMuint8 mode;
} MVMCallCaptureBody;
typedef struct _MVMCallCapture {
    MVMObject common;
    MVMCallCaptureBody body;
} MVMCallCapture;

/* Function for REPR setup. */
MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc);
