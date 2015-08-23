/* Register allocator based on linear-scan allocation. For this
 * algorithm, we need the live ranges of values, sorted ascending and
 * descending by end point. I want to use this algorithm online, so it
 * seems logical to use heaps */
struct MVMJitRegisterAllocator {
    MVM_DYNAR_DECL(MVMJitExprValue*, active_desc);
    MVM_DYNAR_DECL(MVMJitExprValue*, active_asc);
    MVMint8 register_state[16];
};


MVMint8 MVM_jit_register_alloc(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 reg_cls);
void MVM_jit_register_use(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMint32 reg_cls, MVMint8 reg_num, MVMint32 node);
void MVM_jit_register_release(MVMThreadContext *tc, MVMJitCompiler *compiler,
                              MVMint32 reg_cls, MVMint8 reg_num);
void MVM_jit_register_free(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMint32 reg_cls, MVMint8 reg_num);
void MVM_jit_register_take(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMint32 reg_cls, MVMint8 reg_num);
void MVM_jit_register_spill(MVMThreadContext *tc, MVMJitCompiler *copmiler,
                            MVMJitExprTree *tree, MVMint32 node);
void MVM_jit_register_load(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitExprTree *tree, MVMint32 node, MVMint32 reg_cls);
void MVM_jit_register_load_to(MVMThreadContext *tc, MVMJitCompiler *compiler,
                              MVMJitExprTree *tree, MVMint32 node,
                              MVMint32 reg_cls, MVMint8 reg_num);
