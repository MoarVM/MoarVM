/* An argument capture carries a callsite and a set of arguments. The capture
 * indicates how to interpret the arguments. There will never be any flattening
 * arguments at this point; those are resolved earlier. */
struct MVMCaptureBody {
    /* The callsite. */
    MVMCallsite *callsite;
    /* Argument buffer, which is allocated using the FSA. */
    MVMRegister *args;
};
struct MVMCapture {
    MVMObject common;
    MVMCaptureBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCapture_initialize(MVMThreadContext *tc);
MVMObject * MVM_capture_from_args(MVMThreadContext *tc, MVMArgs args);
