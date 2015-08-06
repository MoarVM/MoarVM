#include "moar.h"
#include "internal.h"

/* Stub file to mark our lack of support for this architecture. We
   should probably stub dasm, too, rather than include x86 */
#include "dasm_x86.h"

const MVMint32 MVM_jit_support(void) {
    return 0;
}

const unsigned char * MVM_jit_actions(void) {
    return NULL;
}

const unsigned int MVM_jit_num_globals(void) {
    return 0;
}

void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg) {}
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg) {}
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitCompiler *compiler,
                            MVMJitGraph *jg, MVMJitPrimitive *prim) {}
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                         MVMJitCallC *call_spec) {}
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                         MVMJitBranch *branc_spec) {}
void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                        MVMJitLabel *label) {}
void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                        MVMJitGuard *guard) {}
void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                         MVMJitInvoke *invoke) {}
void MVM_jit_emit_jumplist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                           MVMJitJumpList *jumplist) {}
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                          MVMJitControl *ctrl) {}

