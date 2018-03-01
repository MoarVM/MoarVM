#include "moar.h"

#include <signal.h>
#ifdef _WIN32
#define SIGHUP      1
#define SIGKILL     9
#define SIGWINCH    28
#endif

/* Info we convey about a signal handler. */
typedef struct {
    int               signum;
    uv_signal_t       handle;
    MVMThreadContext *tc;
    int               work_idx;
} SignalInfo;

/* Signal callback; dispatches schedulee to the queue. */
static void signal_cb(uv_signal_t *handle, int sig_num) {
    SignalInfo       *si  = (SignalInfo *)handle->data;
    MVMThreadContext *tc  = si->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, si->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVMROOT2(tc, t, arr, {
        MVMObject *sig_num_boxed = MVM_repr_box_int(tc,
            tc->instance->boot_types.BOOTInt, sig_num);
        MVM_repr_push_o(tc, arr, sig_num_boxed);
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Sets the signal handler up on the event loop. */
static void setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    SignalInfo *si = (SignalInfo *)data;
    uv_signal_init(loop, &si->handle);
    si->work_idx    = MVM_io_eventloop_add_active_work(tc, async_task);
    si->tc          = tc;
    si->handle.data = si;
    uv_signal_start(&si->handle, signal_cb, si->signum);
}

static void cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    SignalInfo *si = (SignalInfo *)data;
    if (si->work_idx >= 0) {
        if (!uv_is_closing((uv_handle_t *)&(si->handle)))
            uv_signal_stop(&si->handle);
        MVM_io_eventloop_remove_active_work(tc, &(si->work_idx));
    }
}

/* Frees data associated with a timer async task. */
static void gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for async timer task. */
static const MVMAsyncTaskOps op_table = {
    setup,
    NULL,
    cancel,
    NULL,
    gc_free
};

#define PROCESS_SIGS(X) \
    X( MVM_SIGHUP    )  \
    X( MVM_SIGINT    )  \
    X( MVM_SIGQUIT   )  \
    X( MVM_SIGILL    )  \
    X( MVM_SIGTRAP   )  \
    X( MVM_SIGABRT   )  \
    X( MVM_SIGEMT    )  \
    X( MVM_SIGFPE    )  \
    X( MVM_SIGKILL   )  \
    X( MVM_SIGBUS    )  \
    X( MVM_SIGSEGV   )  \
    X( MVM_SIGSYS    )  \
    X( MVM_SIGPIPE   )  \
    X( MVM_SIGALRM   )  \
    X( MVM_SIGTERM   )  \
    X( MVM_SIGURG    )  \
    X( MVM_SIGSTOP   )  \
    X( MVM_SIGTSTP   )  \
    X( MVM_SIGCONT   )  \
    X( MVM_SIGCHLD   )  \
    X( MVM_SIGTTIN   )  \
    X( MVM_SIGTTOU   )  \
    X( MVM_SIGIO     )  \
    X( MVM_SIGXCPU   )  \
    X( MVM_SIGXFSZ   )  \
    X( MVM_SIGVTALRM )  \
    X( MVM_SIGPROF   )  \
    X( MVM_SIGWINCH  )  \
    X( MVM_SIGINFO   )  \
    X( MVM_SIGUSR1   )  \
    X( MVM_SIGUSR2   )  \
    X( MVM_SIGTHR    )  \
    X( MVM_SIGSTKFLT )  \
    X( MVM_SIGPWR    )  \
    X( MVM_SIGBREAK  )

#define GEN_ENUMS(v)   v,
#define GEN_STRING(v) #v,

static enum {
    PROCESS_SIGS(GEN_ENUMS)
} MVM_sig_names;

static char const * const SIG_WANTED[] = {
    PROCESS_SIGS(GEN_STRING)
};
static MVMint32 const NUM_SIG_WANTED = sizeof(SIG_WANTED) / sizeof(char const *);

MVMObject * MVM_io_get_signals(MVMThreadContext *tc) {
    MVMInstance  * const instance = tc->instance;
    MVMHLLConfig *       hll      = MVM_hll_current(tc);
    MVMObject    *       sig_hash;

    MVMint8 sig_wanted_vals[NUM_SIG_WANTED];
    for (MVMint8 i = 0; i < NUM_SIG_WANTED; i++) { sig_wanted_vals[i] = 0; }

    if (instance->sig_hash) {
        return instance->sig_hash;
    }

#ifdef SIGHUP
    sig_wanted_vals[MVM_SIGHUP]    = SIGHUP;
#endif
#ifdef SIGINT
    sig_wanted_vals[MVM_SIGINT]    = SIGINT;
#endif
#ifdef SIGQUIT
    sig_wanted_vals[MVM_SIGQUIT]   = SIGQUIT;
#endif
#ifdef SIGILL
    sig_wanted_vals[MVM_SIGILL]    = SIGILL;
#endif
#ifdef SIGTRAP
    sig_wanted_vals[MVM_SIGTRAP]   = SIGTRAP;
#endif
#ifdef SIGABRT
    sig_wanted_vals[MVM_SIGABRT]   = SIGABRT;
#endif
#ifdef SIGEMT
    sig_wanted_vals[MVM_SIGEMT]    = SIGEMT;
#endif
#ifdef SIGFPE
    sig_wanted_vals[MVM_SIGFPE]    = SIGFPE;
#endif
#ifdef SIGKILL
    sig_wanted_vals[MVM_SIGKILL]   = SIGKILL;
#endif
#ifdef SIGBUS
    sig_wanted_vals[MVM_SIGBUS]    = SIGBUS;
#endif
#ifdef SIGSEGV
    sig_wanted_vals[MVM_SIGSEGV]   = SIGSEGV;
#endif
#ifdef SIGSYS
    sig_wanted_vals[MVM_SIGSYS]    = SIGSYS;
#endif
#ifdef SIGPIPE
    sig_wanted_vals[MVM_SIGPIPE]   = SIGPIPE;
#endif
#ifdef SIGALRM
    sig_wanted_vals[MVM_SIGALRM]   = SIGALRM;
#endif
#ifdef SIGTERM
    sig_wanted_vals[MVM_SIGTERM]   = SIGTERM;
#endif
#ifdef SIGURG
    sig_wanted_vals[MVM_SIGURG]    = SIGURG;
#endif
#ifdef SIGSTOP
    sig_wanted_vals[MVM_SIGSTOP]   = SIGSTOP;  /* hammer time */
#endif
#ifdef SIGTSTP
    sig_wanted_vals[MVM_SIGTSTP]   = SIGTSTP;
#endif
#ifdef SIGCONT
    sig_wanted_vals[MVM_SIGCONT]   = SIGCONT;
#endif
#ifdef SIGCHLD
    sig_wanted_vals[MVM_SIGCHLD]   = SIGCHLD;
#endif
#ifdef SIGTTIN
    sig_wanted_vals[MVM_SIGTTIN]   = SIGTTIN;
#endif
#ifdef SIGTTOU
    sig_wanted_vals[MVM_SIGTTOU]   = SIGTTOU;
#endif
#ifdef SIGIO
    sig_wanted_vals[MVM_SIGIO]     = SIGIO;
#endif
#ifdef SIGXCPU
    sig_wanted_vals[MVM_SIGXCPU]   = SIGXCPU;
#endif
#ifdef SIGXFSZ
    sig_wanted_vals[MVM_SIGXFSZ]   = SIGXFSZ;
#endif
#ifdef SIGVTALRM
    sig_wanted_vals[MVM_SIGVTALRM] = SIGVTALRM;
#endif
#ifdef SIGPROF
    sig_wanted_vals[MVM_SIGPROF]   = SIGPROF;
#endif
#ifdef SIGWINCH
    sig_wanted_vals[MVM_SIGWINCH]  = SIGWINCH;
#endif
#ifdef SIGINFO
    sig_wanted_vals[MVM_SIGINFO]   = SIGINFO;
#endif
#ifdef SIGUSR1
    sig_wanted_vals[MVM_SIGUSR1]   = SIGUSR1;
#endif
#ifdef SIGUSR2
    sig_wanted_vals[MVM_SIGUSR2]   = SIGUSR2;
#endif
#ifdef SIGTHR
    sig_wanted_vals[MVM_SIGTHR]    = SIGTHR;
#endif
#ifdef SIGSTKFLT
    sig_wanted_vals[MVM_SIGSTKFLT] = SIGSTKFLT;
#endif
#ifdef SIGPWR
    sig_wanted_vals[MVM_SIGPWR]    = SIGPWR;
#endif
#ifdef SIGBREAK
    sig_wanted_vals[MVM_SIGBREAK]  = SIGBREAK;
#endif


    sig_hash = MVM_repr_alloc_init(tc, hll->slurpy_hash_type);
    MVMROOT(tc, sig_hash, {
        for (MVMint8 i = 0; i < NUM_SIG_WANTED; i++) {
            MVMString *key      = NULL;
            MVMString *full_key = NULL;
            MVMObject *val      = NULL;

            MVMROOT3(tc, key, full_key, val, {
                full_key = MVM_string_utf8_c8_decode(
                    tc, instance->VMString, SIG_WANTED[i], strlen(SIG_WANTED[i])
                );

                key = MVM_string_substring(tc, full_key, 4, -1);
                val = MVM_repr_box_int(tc, hll->int_box_type, sig_wanted_vals[i]);

                MVM_repr_bind_key_o(tc, sig_hash, key, val);
            });
        }

        instance->sig_hash = sig_hash;
    });

    return sig_hash;
}

/* Creates a new timer. */
MVMObject * MVM_io_signal_handle(MVMThreadContext *tc, MVMObject *queue,
                                 MVMObject *schedulee, MVMint64 signal,
                                 MVMObject *async_type) {
    MVMAsyncTask *task;
    SignalInfo   *signal_info;
    int           signum;

    /* Transform the signal number. */
    switch (signal) {
    case MVM_SIG_HUP:       signum = SIGHUP;    break;
    case MVM_SIG_INT:       signum = SIGINT;    break;
#ifdef SIGQUIT
    case MVM_SIG_QUIT:      signum = SIGQUIT;   break;
#endif
#ifdef SIGILL
    case MVM_SIG_ILL:       signum = SIGILL;    break;
#endif
#ifdef SIGTRAP
    case MVM_SIG_TRAP:      signum = SIGTRAP;   break;
#endif
#ifdef SIGABRT
    case MVM_SIG_ABRT:      signum = SIGABRT;   break;
#endif
#ifdef SIGEMT
    case MVM_SIG_EMT:       signum = SIGEMT;    break;
#endif
#ifdef SIGFPE
    case MVM_SIG_FPE:       signum = SIGFPE;    break;
#endif
#ifdef SIGKILL
    case MVM_SIG_KILL:      signum = SIGKILL;   break;
#endif
#ifdef SIGBUS
    case MVM_SIG_BUS:       signum = SIGBUS;    break;
#endif
#ifdef SIGSEGV
    case MVM_SIG_SEGV:      signum = SIGSEGV;   break;
#endif
#ifdef SIGSYS
    case MVM_SIG_SYS:       signum = SIGSYS;    break;
#endif
#ifdef SIGPIPE
    case MVM_SIG_PIPE:      signum = SIGPIPE;   break;
#endif
#ifdef SIGALRM
    case MVM_SIG_ALRM:      signum = SIGALRM;   break;
#endif
#ifdef SIGTERM
    case MVM_SIG_TERM:      signum = SIGTERM;   break;
#endif
#ifdef SIGURG
    case MVM_SIG_URG:       signum = SIGURG;    break;
#endif
#ifdef SIGSTOP
    case MVM_SIG_STOP:      signum = SIGSTOP;   break; /* hammer time */
#endif
#ifdef SIGTSTP
    case MVM_SIG_TSTP:      signum = SIGTSTP;   break;
#endif
#ifdef SIGCONT
    case MVM_SIG_CONT:      signum = SIGCONT;   break;
#endif
#ifdef SIGCHLD
    case MVM_SIG_CHLD:      signum = SIGCHLD;   break;
#endif
#ifdef SIGTTIN
    case MVM_SIG_TTIN:      signum = SIGTTIN;   break;
#endif
#ifdef SIGTTOU
    case MVM_SIG_TTOU:      signum = SIGTTOU;   break;
#endif
#ifdef SIGIO
    case MVM_SIG_IO:        signum = SIGIO;     break;
#endif
#ifdef SIGXCPU
    case MVM_SIG_XCPU:      signum = SIGXCPU;   break;
#endif
#ifdef SIGXFSZ
    case MVM_SIG_XFSZ:      signum = SIGXFSZ;   break;
#endif
#ifdef SIGVTALRM
    case MVM_SIG_VTALRM:    signum = SIGVTALRM; break;
#endif
#ifdef SIGPROF
    case MVM_SIG_PROF:      signum = SIGPROF;   break;
#endif
#ifdef SIGWINCH
    case MVM_SIG_WINCH:     signum = SIGWINCH;  break;
#endif
#ifdef SIGINFO
    case MVM_SIG_INFO:      signum = SIGINFO;   break;
#endif
#ifdef SIGUSR1
    case MVM_SIG_USR1:      signum = SIGUSR1;   break;
#endif
#ifdef SIGUSR2
    case MVM_SIG_USR2:      signum = SIGUSR2;   break;
#endif
#ifdef SIGTHR
    case MVM_SIG_THR:       signum = SIGTHR;    break;
#endif
#ifdef SIGSTKFLT
    case MVM_SIG_STKFLT:    signum = SIGSTKFLT; break;
#endif
#ifdef SIGPWR
    case MVM_SIG_PWR:       signum = SIGPWR;    break;
#endif
#ifdef SIGBREAK
    case MVM_SIG_BREAK:     signum = SIGBREAK;  break;
#endif
    default:
        MVM_exception_throw_adhoc(tc, "Unsupported signal handler %d",
            (int)signal);
    }

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "signal target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "signal result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT2(tc, queue, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops      = &op_table;
    signal_info         = MVM_malloc(sizeof(SignalInfo));
    signal_info->signum = signum;
    task->body.data     = signal_info;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
