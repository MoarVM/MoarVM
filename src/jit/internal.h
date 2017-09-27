/* Internal header for the MoarVM JIT compiler. Probably best not to use it
 * outside the JIT */

#define MVM_JIT_ARCH_X64       1
#define MVM_JIT_PLATFORM_POSIX 1
#define MVM_JIT_PLATFORM_WIN32 2

/* Override dynasm state definitions, so that we can use our own compiler
 * with register allocation structures etc. */
#define Dst_DECL MVMJitCompiler *compiler
#define Dst_REF (compiler->dasm_handle)
#define Dst (compiler)
#include "dasm_proto.h"

struct MVMJitCompiler {
    dasm_State *dasm_handle;
    void      **dasm_globals;
};

/* Declarations for architecture-specific codegen stuff */
const MVMint32 MVM_jit_support(void);
const unsigned char * MVM_jit_actions(void);
const unsigned int MVM_jit_num_globals(void);
void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                MVMJitGraph *jg, MVMJitPrimitive *prim);
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                             MVMJitCallC *call_spec);
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                             MVMJitBranch *branc_spec);
void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                            MVMJitLabel *label);
void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                            MVMJitGuard *guard);
void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                             MVMJitInvoke *invoke);
void MVM_jit_emit_jumplist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                               MVMJitJumpList *jumplist);
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                              MVMJitControl *ctrl);
void MVM_jit_emit_data(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitData *data);
void MVM_jit_emit_save_rv(MVMThreadContext *tc, MVMJitCompiler *compiler);
