/* Representation for an argument capture, with argument processing state. */
struct MVMCallCaptureBody {
    MVMArgProcContext *apc;
};
struct MVMCallCapture {
    MVMObject common;
    MVMCallCaptureBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc);

MVMint64 MVM_capture_pos_primspec(MVMThreadContext *tc, MVMObject *capture, MVMint64 index);
