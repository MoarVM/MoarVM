/* MoarVM platform-independent signal values. */
/* these match the BSD/Darwin values */
#define MVM_SIG_HUP     1
#define MVM_SIG_INT     2
#define MVM_SIG_QUIT    3
#define MVM_SIG_ILL     4
#define MVM_SIG_TRAP    5
#define MVM_SIG_ABRT    6
#define MVM_SIG_EMT     7
#define MVM_SIG_FPE     8
#define MVM_SIG_KILL    9
#define MVM_SIG_BUS     10
#define MVM_SIG_SEGV    11
#define MVM_SIG_SYS     12
#define MVM_SIG_PIPE    13
#define MVM_SIG_ALRM    14
#define MVM_SIG_TERM    15
#define MVM_SIG_URG     16
#define MVM_SIG_STOP    17 /* hammer time */
#define MVM_SIG_TSTP    18
#define MVM_SIG_CONT    19
#define MVM_SIG_CHLD    20
#define MVM_SIG_TTIN    21
#define MVM_SIG_TTOU    22
#define MVM_SIG_IO      23
#define MVM_SIG_XCPU    24
#define MVM_SIG_XFSZ    25
#define MVM_SIG_VTALRM  26
#define MVM_SIG_PROF    27
#define MVM_SIG_WINCH   28
#define MVM_SIG_INFO    29
#define MVM_SIG_USR1    30
#define MVM_SIG_USR2    31
#define MVM_SIG_THR     32

/* linux overloads */
#define MVM_SIG_STKFLT  116
#define MVM_SIG_PWR     130

/* windows overloads */
#define MVM_SIG_BREAK   221

MVMObject * MVM_io_signal_handle(MVMThreadContext *tc, MVMObject *queue,
    MVMObject *schedulee, MVMint64 signal, MVMObject *async_type);
