/* Declarations for architecture-specific codegen stuff */
void MVM_jit_emit_prologue(MVMThreadContext *tc, dasm_State **state);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, dasm_State **state);
