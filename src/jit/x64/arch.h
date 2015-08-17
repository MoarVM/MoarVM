#ifdef MVM_JIT_X64_ARCH_H
#error "arch.h included twice"
#endif
#define MVM_JIT_X64_ARCH_H 1

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


enum {
#define GPRNAME(x) MVM_JIT_X64_ ## x
X64_GPR(GPRNAME)
#undef GPRNAME
};

#define MVM_JIT_NUM_REG_CLASS 2
/* Define the GPR usable for general calculations */
#if MVM_JIT_PLATFORM == MVM_JIT_POSIX

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
#else
/* Microsoft :-( */
#define X64_FREE_GPR(_) \
    _(RAX), \
    _(RCX), \
    _(RDX), \
    _(R8), \
    _(R9), \
    _(R10), \
    _(R11)
#endif

