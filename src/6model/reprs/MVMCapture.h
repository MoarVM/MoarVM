/* An argument capture carries a callsite and a set of arguments. The capture
 * indicates how to interpret the arguments. There will never be any flattening
 * arguments at this point; those are resolved earlier. */
struct MVMCaptureBody {
    /* The callsite. */
    MVMCallsite *callsite;
    /* Argument buffer. */
    MVMRegister *args;
};
struct MVMCapture {
    MVMObject common;
    MVMCaptureBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCapture_initialize(MVMThreadContext *tc);

/* Operations for creating MVMCapture objects from args and vice versa. */
MVMObject * MVM_capture_from_args(MVMThreadContext *tc, MVMArgs args);
MVMArgs MVM_capture_to_args(MVMThreadContext *tc, MVMObject *capture);

/* Operations for accessing arguments in MVMCapture objects. */
MVMint64 MVM_capture_num_pos_args(MVMThreadContext *tc, MVMObject *capture);
MVMint64 MVM_capture_num_args(MVMThreadContext *tc, MVMObject *capture);
MVMint64 MVM_capture_arg_pos_primspec(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMint64 MVM_capture_arg_primspec(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
void MVM_capture_arg_pos(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out);
void MVM_capture_arg(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out);
MVMObject * MVM_capture_arg_pos_o(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMObject * MVM_capture_arg_o(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMString * MVM_capture_arg_pos_s(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMint64 MVM_capture_arg_pos_i(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMuint64 MVM_capture_arg_pos_u(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMnum64 MVM_capture_arg_pos_n(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);
MVMint64 MVM_capture_has_named_arg(MVMThreadContext *tc, MVMObject *capture, MVMString *name);
MVMObject * MVM_capture_get_names_list(MVMThreadContext *tc, MVMObject *capture);
MVMObject * MVM_capture_get_nameds(MVMThreadContext *tc, MVMObject *capture);
MVMint64 MVM_capture_has_nameds(MVMThreadContext *tc, MVMObject *capture);
void MVM_capture_arg_by_flag_index(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out);
MVMint64 MVM_capture_is_literal_arg(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx);

/* Operations for deriving a new MVMCapture from an existing one. */
MVMObject * MVM_capture_drop_args(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx, MVMuint32 count);
MVMObject * MVM_capture_insert_arg(MVMThreadContext *tc, MVMObject *capture, MVMuint32 idx,
        MVMCallsiteFlags kind, MVMRegister value);
MVMObject * MVM_capture_replace_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMCallsiteEntry kind, MVMRegister value);
