/* Internal header for the MoarVM JIT compiler. Probably best not to use it
 * outside the JIT */

#define MVM_JIT_ARCH_STUB      0
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
    MVMJitGraph   *graph;

    MVMint32    label_offset;
    MVMint32    label_max;

    /* For spilling values that don't fit into the register allocator */
    MVMint32    spill_bottom;
    MVMint32    spill_top;
};

/* Declarations for architecture-specific codegen stuff */
MVM_PUBLIC const MVMint32 MVM_jit_support(void);
const unsigned char * MVM_jit_actions(void);
const unsigned int MVM_jit_num_globals(void);
void MVM_jit_emit_prologue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_emit_epilogue(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg);
void MVM_jit_emit_primitive(MVMThreadContext *tc, MVMJitCompiler *compiler,
                            MVMJitGraph *jg, MVMJitPrimitive *prim);
void MVM_jit_emit_call_c(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                         MVMJitCallC *call_spec);
void MVM_jit_emit_branch(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 label);
void MVM_jit_emit_conditional_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                     MVMint32 cond, MVMint32 label);
void MVM_jit_emit_block_branch(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                               MVMJitBranch *branch_spec);
void MVM_jit_emit_label(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                        MVMint32 label);
void MVM_jit_emit_guard(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                        MVMJitGuard *guard);
void MVM_jit_emit_invoke(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                         MVMJitInvoke *invoke);
void MVM_jit_emit_jumplist(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                           MVMJitJumpList *jumplist);
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitGraph *jg,
                          MVMJitControl *ctrl);


void MVM_jit_emit_load(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 location,
                       MVMJitStorageClass st_cls, MVMint8 st_pos, MVMint32 size);
void MVM_jit_emit_spill(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 location,
                        MVMJitStorageClass st_cls, MVMint8 st_pos, MVMint32 size);
void MVM_jit_emit_copy(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 dst_reg_cls,
                       MVMint8 dst_reg_num, MVMint32 src_reg_cls, MVMint8 src_reg_num);
void MVM_jit_emit_stack_arg(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 stack_pos,
                            MVMint32 reg_cls, MVMint8 reg_num, MVMint32 size);
void MVM_jit_emit_marker(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 num);
#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
#include "jit/x64/arch.h"
#endif
