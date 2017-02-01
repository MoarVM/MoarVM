#include "moar.h"
#include "dasm_proto.h"
#include "dasm_x86.h"
#include "emit.h"
/* Stub file to mark our lack of support for this architecture. We
   should probably stub dasm, too, rather than include x86 */

const MVMint32 MVM_jit_support(void) {
    return 0;
}

const unsigned char * MVM_jit_actions(void) {
    return NULL;
}

const unsigned int MVM_jit_num_globals(void) {
    return 0;
}

void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {}
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitGraph *jg,
                           dasm_State **Dst) {}
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                            MVMJitPrimitive *prim, dasm_State **Dst) {}
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitCallC *call_spec, dasm_State **Dst) {}
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitBranch *branch_spec, dasm_State **Dst) {}
void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitLabel *label, dasm_State **Dst) {}
void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitGraph *jg,
                        MVMJitGuard *guard, dasm_State **Dst) {}
void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMJitInvoke *invoke, dasm_State **Dst) {}
void MVM_jit_emit_jumplist(MVMThreadContext *tc, MVMJitGraph *jg,
                           MVMJitJumpList *jumplist, dasm_State **Dst) {}
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMJitControl *ctrl, dasm_State **Dst) {}
void MVM_jit_emit_data(MVMThreadContext *tc, MVMJitGraph *jg, MVMJitData *data, dasm_State **Dst) {}
