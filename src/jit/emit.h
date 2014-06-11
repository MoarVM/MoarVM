/* Declarations for architecture-specific codegen stuff */
const unsigned char * MVM_jit_actions(void);
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst);
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitPrimitive *prim,
                            dasm_State **Dst);
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCallC *call_spec,
                         dasm_State **Dst);
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitBranch *branc_spec,
                         dasm_State **Dst);
void MVM_jit_emit_label(MVMThreadContext *tc, MVMint32 label,
                        dasm_State **Dst);
