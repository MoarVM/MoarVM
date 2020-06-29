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

/* Operations for creating MVMCapture objects. */
MVMObject * MVM_capture_from_args(MVMThreadContext *tc, MVMArgs args);

/* Operations for accessing arguments in MVMCapture objects. */
MVMint64 MVM_capture_num_args(MVMThreadContext *tc, MVMObject *capture);
void MVM_capture_arg_pos(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out);
MVMObject * MVM_capture_arg_pos_o(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMString * MVM_capture_arg_pos_s(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMint64 MVM_capture_arg_pos_i(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMnum64 MVM_capture_arg_pos_n(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);

/* Operations for deriving a new MVMCapture from an existing one. */
MVMObject * MVM_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMObject * MVM_capture_insert_arg(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMCallsiteFlags kind, MVMRegister value);
