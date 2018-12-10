
#define MVM_JIT_ARCH_GPR(_) \
    _(RAX) __COMMA__ \
    _(RCX) __COMMA__ \
    _(RDX) __COMMA__ \
    _(RBX) __COMMA__ \
    _(RSP) __COMMA__ \
    _(RBP) __COMMA__ \
    _(RSI) __COMMA__ \
    _(RDI) __COMMA__ \
    _(R8) __COMMA__ \
    _(R9) __COMMA__ \
    _(R10) __COMMA__ \
    _(R11) __COMMA__ \
    _(R12) __COMMA__ \
    _(R13) __COMMA__ \
    _(R14) __COMMA__ \
    _(R15)


#define MVM_JIT_ARCH_FPR(_) \
    _(XMM0) __COMMA__ \
    _(XMM1) __COMMA__ \
    _(XMM2) __COMMA__ \
    _(XMM3) __COMMA__ \
    _(XMM4) __COMMA__ \
    _(XMM5) __COMMA__ \
    _(XMM6) __COMMA__ \
    _(XMM7) __COMMA__ \
    _(XMM8) __COMMA__ \
    _(XMM9) __COMMA__ \
    _(XMM10) __COMMA__ \
    _(XMM11) __COMMA__ \
    _(XMM12) __COMMA__ \
    _(XMM13) __COMMA__ \
    _(XMM14) __COMMA__ \
    _(XMM15)


#if MVM_JIT_PLATFORM == MVM_JIT_PLATFORM_POSIX
/* Define the GPR set usable for general calculations. RAX is reserved for
 * internal use by tiles */

#define MVM_JIT_ARCH_AVAILABLE_GPR(_) \
    _(RCX) __COMMA__ \
    _(RDX) __COMMA__ \
    _(RSI) __COMMA__ \
    _(RDI) __COMMA__ \
    _(R8) __COMMA__ \
    _(R9) __COMMA__ \
    _(R10) __COMMA__ \
    _(R11)

/* define set of non-volatile regsiters */


#define MVM_JIT_ARCH_NONVOLATILE_GPR(_) \
    _(RBX) __COMMA__ \
    _(RSP) __COMMA__ \
    _(RBP) __COMMA__ \
    _(R12) __COMMA__ \
    _(R13) __COMMA__ \
    _(R14) __COMMA__ \
    _(R15)

/* GPR used for arguments */
#define MVM_JIT_ARCH_ARG_GPR(_) \
    _(RDI) __COMMA__ \
    _(RSI) __COMMA__ \
    _(RDX) __COMMA__ \
    _(RCX) __COMMA__ \
    _(R8) __COMMA__ \
    _(R9)

/* SSE used for arguments */

#define MVM_JIT_ARCH_ARG_FPR(_) \
    _(XMM0) __COMMA__ \
    _(XMM1) __COMMA__ \
    _(XMM2) __COMMA__ \
    _(XMM3) __COMMA__ \
    _(XMM4) __COMMA__ \
    _(XMM5) __COMMA__ \
    _(XMM6) __COMMA__ \
    _(XMM7)

#else

/* Microsoft why you give us so few registers :-( */
#define MVM_JIT_ARCH_AVAILABLE_GPR(_) \
    _(RCX) __COMMA__ \
    _(RDX) __COMMA__ \
    _(R8) __COMMA__ \
    _(R9) __COMMA__ \
    _(R10) __COMMA__ \
    _(R11)

#define MVM_JIT_ARCH_NONVOLATILE_GPR(_) \
    _(RBX) __COMMA__ \
    _(RSP) __COMMA__ \
    _(RBP) __COMMA__ \
    _(RSI) __COMMA__ \
    _(RDI) __COMMA__ \
    _(R12) __COMMA__ \
    _(R13) __COMMA__ \
    _(R14) __COMMA__ \
    _(R15)
#define MVM_JIT_ARCH_ARG_GPR(_) \
    _(RCX) __COMMA__ \
    _(RDX) __COMMA__ \
    _(R8) __COMMA__ \
    _(R9)
#define MVM_JIT_ARCH_ARG_FPR(_) \
    _(XMM0) __COMMA__ \
    _(XMM1) __COMMA__ \
    _(XMM2) __COMMA__ \
    _(XMM3)
#endif

/* Frame declarations */
#define MVM_JIT_ARCH_NUM_GPR 16
#define MVM_JIT_ARCH_NUM_FPR 16
