MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph, size_t *codesize_out);
MVMuint8* MVM_jit_magic_bytecode(MVMThreadContext *tc);
void MVM_enter_jit(MVMThreadContext *tc, MVMFrame *frame, MVMJitCode jitcode);
