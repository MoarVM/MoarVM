/* Declarations for architecture-specific codegen stuff */
void MVM_jit_gen_prologue(dasm_State **state, MVMCallSite * callsite);
void MVM_jit_gen_epilogue(dasm_State **state, MVMCallSite * callsite);
