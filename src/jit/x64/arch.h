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


#define MVM_JIT_ARCH_NUM(_) \
    _(XMM0), \
    _(XMM1), \
    _(XMM2), \
    _(XMM3), \
    _(XMM4), \
    _(XMM5), \
    _(XMM6), \
    _(XMM7)


#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX
/* Define the GPR set usable for general calculations */

#define MVM_JIT_ARCH_AVAILABLE_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(RSI), \
    _(RDI), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11)

/* define set of non-volatile regsiters */


#define MVM_JIT_ARCH_NONVOLATILE_GPR(_) \
    _(RBX), \
    _(RSP), \
    _(RBP), \
    _(R12), \
    _(R13), \
    _(R14), \
    _(R15)

/* GPR used for arguments */
#define MVM_JIT_ARCH_ARG_GPR(_) \
    _(RDI), \
    _(RSI), \
    _(RDX), \
    _(RCX), \
    _(R8), \
    _(R9)

/* SSE used for arguments */

#define MVM_JIT_ARCH_ARG_NUM(_) \
    MVM_JIT_ARCH_NUM(_)

#else

/* Microsoft why you give us so few registers :-( */
#define MVM_JIT_ARCH_AVAILABLE_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11)
#define MVM_JIT_ARCH_NONVOLATILE_GPR(_) \
    _(RBX), \
    _(RSP), \
    _(RBP), \
    _(RSI), \
    _(RDI), \
    _(R12), \
    _(R13), \
    _(R14), \
    _(R15)
#define MVM_JIT_ARCH_ARG_GPR(_) \
    _(RCX), \
    _(RDX), \
    _(R8), \
    _(R9)
#define MVM_JIT_ARCH_ARG_NUM(_) \
    _(XMM0), \
    _(XMM1), \
    _(XMM2), \
    _(XMM3)
#endif

/* Frame declarations */
#define MVM_JIT_ARCH_NUM_GPR 16
