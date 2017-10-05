#include "moar.h"
#include "jit/internal.h"

#define MVM_JIT_PLATFORM_POSIX 1
#define MVM_JIT_PLATFORM_WIN32 2

#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX

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
    MVMint32 narg = tree->nodes[arglist_node + 1];
    MVMint32 i, ngpr = 0, nfpr = 0, nstack = 0;
    for (i = 0; i < narg; i++) {
        MVMint32 carg_node = tree->nodes[arglist_node + 2 + i];
        MVMint32 carg_type = tree->nodes[carg_node + 2];
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
    MVMint32 i, narg = tree->nodes[arglist_node + 1];
    for (i = 0; i < MIN(narg, 4); i++) {
        MVMint32 carg_node = tree->nodes[arglist_node + 2 + i];
        MVMint32 carg_type = tree->nodes[carg_node + 2];
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
