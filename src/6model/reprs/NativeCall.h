/* Representation for a native callsite. */
struct MVMNativeCallBody {
    char       *lib_name;
    DLLib      *lib_handle;
    void       *entry_point;
    MVMint16    convention;
    MVMint16    num_args;
    MVMint16    ret_type;
    MVMint16   *arg_types;
    MVMObject **arg_info;
};
struct MVMNativeCall {
    MVMObject common;
    MVMNativeCallBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMNativeCall_initialize(MVMThreadContext *tc);
