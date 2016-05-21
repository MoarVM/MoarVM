/* register classes */
#define MVM_JIT_X64_GPR 0
#define MVM_JIT_X64_SSE 1

#define X64_GPR(_) \
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


#define X64_SSE(_) \
    _(XMM0), \
    _(XMM1), \
    _(XMM2), \
    _(XMM3), \
    _(XMM4), \
    _(XMM5), \
    _(XMM6), \
    _(XMM7)

#define MVM_JIT_REGNAME(x) MVM_JIT_X64_ ## x

enum {
X64_GPR(MVM_JIT_REGNAME)
};

enum {
X64_SSE(MVM_JIT_REGNAME)
};

#define MVM_JIT_NUM_REGCLS 2
#define MVM_JIT_REGCLS_GPR MVM_JIT_X64_GPR
#define MVM_JIT_REGCLS_NUM MVM_JIT_X64_SSE

#define MVM_JIT_RETVAL_GPR MVM_JIT_X64_RAX
#define MVM_JIT_RETVAL_NUM MVM_JIT_X64_XMM0

#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX
/* Define the GPR set usable for general calculations */

#define X64_FREE_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(RSI), \
    _(RDI), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11)

/* GPR used for arguments */

#define X64_ARG_GPR(_) \
    _(RDI), \
    _(RSI), \
    _(RDX), \
    _(RCX), \
    _(R8), \
    _(R9)

/* SSE used for arguments */

#define X64_ARG_SSE(_) \
    X64_SSE(_)


#else

/* Microsoft why you give us so few registers :-( */
#define X64_FREE_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11)
#define X64_ARG_GPR(_) \
    _(RCX), \
    _(RDX), \
    _(R8), \
    _(R9)
#define X64_ARG_SSE(_) \
    _(XMM0), \
    _(XMM1), \
    _(XMM2), \
    _(XMM3)
#endif

/* Frame declarations */
#define MVM_JIT_REG_TC MVM_JIT_X64_R14
#define MVM_JIT_REG_CU MVM_JIT_X64_R13
#define MVM_JIT_REG_LOCAL MVM_JIT_X64_RBX
#define MVM_JIT_REG_STACK MVM_JIT_X64_RSP
