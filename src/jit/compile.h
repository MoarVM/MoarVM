

struct MVMJitCode {
    MVMJitFunc func_ptr;
    size_t         size;
    MVMuint8  *bytecode;
    MVMint16 num_locals;
    MVMStaticFrame  *sf;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);
void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode * code);
