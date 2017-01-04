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
    MVMAsyncTask     *t   = (MVMAsyncTask *)MVM_repr_at_pos_o(tc,
        tc->instance->event_loop_active, si->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVMROOT(tc, t, {
    MVMROOT(tc, arr, {
        MVMObject *sig_num_boxed = MVM_repr_box_int(tc,
            tc->instance->boot_types.BOOTInt, sig_num);
        MVM_repr_push_o(tc, arr, sig_num_boxed);
    });
    });
    MVM_repr_push_o(tc, t->body.queue, arr);
}

/* Sets the signal handler up on the event loop. */
static void setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    SignalInfo *si = (SignalInfo *)data;
    uv_signal_init(loop, &si->handle);
    si->work_idx    = MVM_repr_elems(tc, tc->instance->event_loop_active);
    si->tc          = tc;
    si->handle.data = si;
    MVM_repr_push_o(tc, tc->instance->event_loop_active, async_task);
    uv_signal_start(&si->handle, signal_cb, si->signum);
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
    NULL,
    gc_free
};

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
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
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
