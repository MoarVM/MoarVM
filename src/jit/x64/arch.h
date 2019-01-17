/* Declaration of architecture specific register names */

#define MVM_JIT_ARCH_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(RBX), \
    _(RSP), \
    _(RBP), \
    _(RSI), \
    _(RDI), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11), \
    _(R12), \
    _(R13), \
    _(R14), \
    _(R15)

#define MVM_JIT_ARCH_FPR(_) \
    _(XMM0), \
    _(XMM1), \
    _(XMM2), \
    _(XMM3), \
    _(XMM4), \
    _(XMM5), \
    _(XMM6), \
    _(XMM7), \
    _(XMM8), \
    _(XMM9), \
    _(XMM10), \
    _(XMM11), \
    _(XMM12), \
    _(XMM13), \
    _(XMM14), \
    _(XMM15)

#define MVM_JIT_ARCH_NUM_REG 32
