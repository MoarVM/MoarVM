/* Declarations for architecture-specific codegen stuff */
const unsigned char * MVM_jit_actions(void);
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **Dst);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **Dst);
void MVM_jit_emit_instruction(MVMThreadContext *tc, MVMSpeshIns *ins, dasm_State **Dst);
void MVM_jit_emit_c_call(MVMThreadContext *tc, MVMJitCallC *call_spec, dasm_State **Dst);
