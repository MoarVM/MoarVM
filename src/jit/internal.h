/* Internal header for the MoarVM JIT compiler. Probably best not to use it
 * outside the JIT */

/* Override dynasm state definitions, so that we can use our own compiler
 * with register allocation structures etc. */
#define Dst_DECL MVMJitCompiler *compiler
#define Dst_REF (compiler->dasm_handle)
#define Dst (compiler)
#include "dasm_proto.h"

#define MVM_JIT_MAX_GLOBALS 1

struct MVMJitCompiler {
    dasm_State *dasm_handle;
    MVMJitGraph   *graph;

    MVMint32    label_offset;

    /* For spilling values that don't fit into the register allocator */
    MVMint32    spills_base;
    MVMint32    spills_free[4];
    MVM_VECTOR_DECL(struct { MVMint8 reg_type; MVMint32 next; }, spills);

    void *dasm_globals[MVM_JIT_MAX_GLOBALS];
};

/* Declarations for architecture-specific codegen stuff */
const MVMint32 MVM_jit_support(void);
const unsigned char * MVM_jit_actions(void);
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
void MVM_jit_emit_control(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitControl *ctrl, MVMJitTile *tile);
void MVM_jit_emit_data(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitData *data);

void MVM_jit_emit_load(MVMThreadContext *tc, MVMJitCompiler *compiler,
                       MVMint32 reg_cls, MVMint8 reg_dst,
                       MVMint32 mem_cls, MVMint32 mem_src, MVMint32 size);
void MVM_jit_emit_store(MVMThreadContext *tc, MVMJitCompiler *compiler,
                        MVMint32 mem_cls, MVMint32 mem_pos,
                        MVMint32 reg_cls, MVMint8 reg_pos, MVMint32 size);
void MVM_jit_emit_copy(MVMThreadContext *tc, MVMJitCompiler *compiler,
                       MVMint32 dst_cls, MVMint8 dst_reg, MVMint32 src_cls, MVMint8 src_num);
void MVM_jit_emit_marker(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 num);

MVMint32 MVM_jit_spill_memory_select(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint8 reg_type);
void MVM_jit_spill_memory_release(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMint32 pos, MVMint8 reg_type);




/* Although we use these only symbolically, we need to assign a temporary value
 * in order to to distinguish between them */
#define MVM_JIT_ARCH_X64 1
#define MVM_JIT_PLATFORM_POSIX 1
#define MVM_JIT_PLATFORM_WIN32 2

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
#define MVM_JIT_ARCH_H "jit/x64/arch.h"
#endif

/* Depends on values of MVM_JIT_PLATFORM, so need to be defined, but uses the
 * MVM_JIT_ARCH names literally, so these need to be undefined. */
#ifdef MVM_JIT_ARCH_H
#include MVM_JIT_ARCH_H
#endif

#undef MVM_JIT_ARCH_X64
#undef MVM_JIT_PLATFORM_POSIX
#undef MVM_JIT_PLATFORM_WIN32

enum {
    MVM_JIT_ARCH_GPR(MVM_JIT_REG),
    MVM_JIT_ARCH_FPR(MVM_JIT_REG)
};

extern const MVMBitmap MVM_JIT_AVAILABLE_REGISTERS;
extern const MVMBitmap MVM_JIT_RESERVED_REGISTERS;
extern const MVMBitmap MVM_JIT_SPARE_REGISTERS;

/* We need max and min macros, they used to be in libtommath, but aren't anymore */
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define MVM_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
