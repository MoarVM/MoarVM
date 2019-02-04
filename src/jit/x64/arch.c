#include "moar.h"
#include "jit/internal.h"

#define MVM_JIT_PLATFORM_POSIX 1
#define MVM_JIT_PLATFORM_WIN32 2

const MVMBitmap MVM_JIT_REGISTER_CLASS[] = {
    /* none */ 0,
    /* gpr  */ 0x0000ffff,
    /* fpr  */ 0xffff0000
};

#define R(x) (1<<MVM_JIT_REG(R ## x))
#define XMM(n) (1<<MVM_JIT_REG(XMM ## n))

const MVMBitmap MVM_JIT_SPARE_REGISTERS = R(AX)|XMM(0);

#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX

/* rbx(3), rsp(4), rbp(5), r12, r13, r14, r15 */
const MVMBitmap MVM_JIT_RESERVED_REGISTERS =
    R(BX)|R(SP)|R(BP)|R(12)|R(13)|R(14)|R(15);

/* rcx(1), rdx(2), rsi(6), rdi(7), r8,r9,r10,r11 */
const MVMBitmap MVM_JIT_AVAILABLE_REGISTERS =
    R(CX)|R(DX)|R(SI)|R(DI)|R(8)|R(9)|R(10)|R(11)|
    XMM(1)|XMM(2)|XMM(3)|XMM(4)|XMM(5)|XMM(6)|XMM(7);

static const MVMint8 arg_gpr[] = {
    MVM_JIT_REG(RDI),
    MVM_JIT_REG(RSI),
    MVM_JIT_REG(RDX),
    MVM_JIT_REG(RCX),
    MVM_JIT_REG(R8),
    MVM_JIT_REG(R9),
};

static const MVMint8 arg_fpr[] = {
    MVM_JIT_REG(XMM0),
    MVM_JIT_REG(XMM1),
    MVM_JIT_REG(XMM2),
    MVM_JIT_REG(XMM3),
    MVM_JIT_REG(XMM4),
    MVM_JIT_REG(XMM5),
    MVM_JIT_REG(XMM6),
    MVM_JIT_REG(XMM7),
};


void MVM_jit_arch_storage_for_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                      MVMJitExprTree *tree, MVMint32 arglist_node,
                                      MVMJitStorageRef *storage) {
    MVMint32 narg = MVM_JIT_EXPR_NCHILD(tree, arglist_node);
    MVMint32 *args = MVM_JIT_EXPR_LINKS(tree, arglist_node);
    MVMint32 i, ngpr = 0, nfpr = 0, nstack = 0;
    for (i = 0; i < narg; i++) {
        MVMint32 carg_type = MVM_JIT_EXPR_ARGS(tree, args[i])[0];
        /* posix stores numeric args in floating point registers, everything
         * else in general purpose registers, until it doesn't fit anymore, in
         * which case it stores them on the stack */
        if (carg_type == MVM_JIT_NUM && nfpr < sizeof(arg_fpr)) {
            storage[i]._cls = MVM_JIT_STORAGE_FPR;
            storage[i]._pos = arg_fpr[nfpr++];
        } else if (ngpr < sizeof(arg_gpr)) {
            storage[i]._cls = MVM_JIT_STORAGE_GPR;
            storage[i]._pos = arg_gpr[ngpr++];
        } else {
            storage[i]._cls = MVM_JIT_STORAGE_STACK;
            storage[i]._pos = 8 * nstack++;
        }
    }
}


#elif MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_WIN32

/* rbx(3), rsp(4), rbp(5), rsi(6), rsi(8), r12, r13, r14, r15 */
const MVMBitmap MVM_JIT_RESERVED_REGISTERS =
    R(BX)|R(SP)|R(BP)|R(SI)|R(DI)|R(12)|R(13)|R(14)|R(15);
/* rcx(1), rdx(2), r8,r9,r10,r11 */
const MVMBitMap MVM_JIT_AVAILABLE_REGISTERS =
    R(CX)|R(DX)|R(8)|R(9)|R(10)|R(11)|R(12)|XMM(1)|XMM(2)|XMM(3)|XMM(4)|XMM(5);

static const MVMint8 arg_gpr[] = {
    MVM_JIT_REG(RCX),
    MVM_JIT_REG(RDX),
    MVM_JIT_REG(R8),
    MVM_JIT_REG(R9),
};

static const MVMint8 arg_fpr[] = {
    MVM_JIT_REG(XMM0),
    MVM_JIT_REG(XMM1),
    MVM_JIT_REG(XMM2),
    MVM_JIT_REG(XMM3),
};


void MVM_jit_arch_storage_for_arglist(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                      MVMJitExprTree *tree, MVMint32 arglist_node,
                                      MVMJitStorageRef *storage) {
    MVMint32 i, narg = MVM_JIT_EXPR_NCHILD(tree, arglist_node);
    MVMint32 *args = MVM_JIT_EXPR_LINKS(tree, arglist_node);
    for (i = 0; i < MIN(narg, 4); i++) {
        MVMint32 carg_type = MVM_JIT_EXPR_ARGS(tree, args[i])[0];
        if (carg_type == MVM_JIT_NUM) {
            storage[i]._cls = MVM_JIT_STORAGE_FPR;
            storage[i]._pos = arg_fpr[i];
        } else {
            storage[i]._cls = MVM_JIT_STORAGE_GPR;
            storage[i]._pos = arg_gpr[i];
        }
    }
    /* rest of arguments is passed on stack. first 4 quadwords (32 bytes) are
     * reserved for first 4 args, hence we start counting from 4 upwards.
     * See https://msdn.microsoft.com/en-us/library/ms235286.aspx */
    for (; i < narg; i++) {
        storage[i]._cls = MVM_JIT_STORAGE_STACK;
        storage[i]._pos = i * 8;
    }

}

#else
#error "Unknown platform " MVM_JIT_PLATFORM
#endif
