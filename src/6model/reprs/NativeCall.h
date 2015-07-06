/* Representation for a native callsite. */
struct MVMNativeCallBody {
    char       *lib_name;
    DLLib      *lib_handle;
    void       *entry_point;
#ifdef HAVE_LIBFFI
    ffi_abi     convention;
    ffi_type  **ffi_arg_types;
    ffi_type   *ffi_ret_type;
#else
    MVMint16    convention;
#endif
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
