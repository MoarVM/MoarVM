/* Declarations for architecture-specific codegen stuff */
MVM_PUBLIC const MVMint32 MVM_jit_support(void);
const unsigned char * MVM_jit_actions(void);
const unsigned int MVM_jit_num_globals(void);
void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst);
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                            MVMJitPrimitive *prim, dasm_State **Dst);
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitCallC *call_spec, dasm_State **Dst);
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch *branc_spec, dasm_State **Dst);
void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitLabel *label, dasm_State **Dst);
void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitGuard *guard, dasm_State **Dst);
void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitInvoke *invoke, dasm_State **Dst);
void MVM_jit_emit_jumplist(MVMThreadContext *tc, MVMJitGraph *jg,
                           MVMJitJumpList *jumplist, dasm_State **Dst);
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMJitControl *ctrl, dasm_State **Dst);
