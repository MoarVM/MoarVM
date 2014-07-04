typedef MVMint32 (*MVMJitFunc)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);

struct MVMJitCode {
    MVMJitFunc func_ptr;
    size_t         size;
    MVMuint8  *bytecode;
    MVMint16 num_locals;
    MVMStaticFrame  *sf;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);
void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
MVMint32 MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                            MVMJitCode * code);

#define MVM_JIT_CTRL_DEOPT -1
#define MVM_JIT_CTRL_NORMAL 0
