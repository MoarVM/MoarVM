#include "moar.h"
/* Stub file to mark our lack of support for this architecture. We
   should probably stub dasm, too, rather than include x86 */

const MVMint32 MVM_jit_support() {
    return 0;
}

MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    return NULL;
}
MVMJitCode * MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *jg) {
    return NULL;
}

void MVM_jit_graph_destroy(MVMThreadContext *tc, MVMJitGraph *graph) {
    return;
}

void MVM_jit_code_destroy(MVMThreadContext *tc, MVMJitCode *code) {
    return;
}

void MVM_jit_code_enter(MVMThreadContext *tc, MVMCompUnit *cu, MVMJitCode *code) {
    return;
}

MVMint32 MVM_jit_code_get_active_handlers(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame, MVMint32 *handlers) {
    return 0;
}

MVMint32 MVM_jit_code_get_active_inlines(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame,  MVMint32 *inlines) {
    return 0;
}

MVMint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame) {
    return 0;
}
