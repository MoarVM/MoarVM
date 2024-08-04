#include "moar.h"

#include <signal.h>
#ifdef _WIN32
/* Add signals used by libuv but not defined in the Windows signal.h */
#define SIGHUP      1
#define SIGKILL     9
#define SIGBREAK    21
#define SIGWINCH    28
#endif

/* Info we convey about a signal handler. */
typedef struct {
    int               signum;
    uv_signal_t       handle;
    MVMThreadContext *tc;
    int               work_idx;
    MVMObject        *setup_notify_queue;
    MVMObject        *setup_notify_schedulee;
} SignalInfo;

/* Signal callback; dispatches schedulee to the queue. */
static void signal_cb(uv_signal_t *handle, int sig_num) {
    SignalInfo       *si  = (SignalInfo *)handle->data;
    MVMThreadContext *tc  = si->tc;
    MVMObject        *arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
    MVMAsyncTask     *t   = MVM_io_eventloop_get_active_work(tc, si->work_idx);
    MVM_repr_push_o(tc, arr, t->body.schedulee);
    MVMROOT2(tc, t, arr) {
        MVMObject *sig_num_boxed = MVM_repr_box_int(tc,
            tc->instance->boot_types.BOOTInt, sig_num);
        MVM_repr_push_o(tc, arr, sig_num_boxed);
    }
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

    if (si->setup_notify_queue && si->setup_notify_schedulee) {
        MVMObject *arr;
        MVMROOT(tc, async_task) {
            arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
        }
        MVM_repr_push_o(tc, arr, si->setup_notify_schedulee);
        MVM_repr_push_o(tc, si->setup_notify_queue, arr);
    }
}

static void free_on_close_cb(uv_handle_t *handle) {
    SignalInfo *si = (SignalInfo*)handle->data;
    MVM_io_eventloop_remove_active_work(si->tc, &(si->work_idx));
}

static void cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    SignalInfo *si = (SignalInfo *)data;
    if (si->work_idx >= 0) {
        if (!uv_is_closing((uv_handle_t *)&(si->handle)))
            uv_signal_stop(&si->handle);
        uv_close((uv_handle_t*)&si->handle, free_on_close_cb);
    }
}

static void gc_mark(MVMThreadContext *tc, void *data, MVMGCWorklist *worklist) {
    SignalInfo *si = (SignalInfo *)data;
    MVM_gc_worklist_add(tc, worklist, &si->setup_notify_queue);
    MVM_gc_worklist_add(tc, worklist, &si->setup_notify_schedulee);
}

/* Frees data associated with a signal async task. */
static void gc_free(MVMThreadContext *tc, MVMObject *t, void *data) {
    if (data)
        MVM_free(data);
}

/* Operations table for async signal task. */
static const MVMAsyncTaskOps op_table = {
    setup,
    NULL,
    cancel,
    gc_mark,
    gc_free
};

#define NUM_SIG_WANTED  35
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

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
static enum {
    PROCESS_SIGS(GEN_ENUMS)
} MVM_sig_names;
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif


static char const * const SIG_WANTED[NUM_SIG_WANTED] = {
    PROCESS_SIGS(GEN_STRING)
};

static void populate_sig_values(MVMint8 *sig_vals) {
    MVMint8 i;
    for (i = 0; i < NUM_SIG_WANTED; i++) { sig_vals[i] = 0; }

#ifdef SIGHUP
    sig_vals[MVM_SIGHUP]    = SIGHUP;
#endif
#ifdef SIGINT
    sig_vals[MVM_SIGINT]    = SIGINT;
#endif
#ifdef SIGQUIT
    sig_vals[MVM_SIGQUIT]   = SIGQUIT;
#endif
#ifdef SIGILL
    sig_vals[MVM_SIGILL]    = SIGILL;
#endif
#ifdef SIGTRAP
    sig_vals[MVM_SIGTRAP]   = SIGTRAP;
#endif
#ifdef SIGABRT
    sig_vals[MVM_SIGABRT]   = SIGABRT;
#endif
#ifdef SIGEMT
    sig_vals[MVM_SIGEMT]    = SIGEMT;
#endif
#ifdef SIGFPE
    sig_vals[MVM_SIGFPE]    = SIGFPE;
#endif
#ifdef SIGKILL
    sig_vals[MVM_SIGKILL]   = SIGKILL;
#endif
#ifdef SIGBUS
    sig_vals[MVM_SIGBUS]    = SIGBUS;
#endif
#ifdef SIGSEGV
    sig_vals[MVM_SIGSEGV]   = SIGSEGV;
#endif
#ifdef SIGSYS
    sig_vals[MVM_SIGSYS]    = SIGSYS;
#endif
#ifdef SIGPIPE
    sig_vals[MVM_SIGPIPE]   = SIGPIPE;
#endif
#ifdef SIGALRM
    sig_vals[MVM_SIGALRM]   = SIGALRM;
#endif
#ifdef SIGTERM
    sig_vals[MVM_SIGTERM]   = SIGTERM;
#endif
#ifdef SIGURG
    sig_vals[MVM_SIGURG]    = SIGURG;
#endif
#ifdef SIGSTOP
    sig_vals[MVM_SIGSTOP]   = SIGSTOP;  /* hammer time */
#endif
#ifdef SIGTSTP
    sig_vals[MVM_SIGTSTP]   = SIGTSTP;
#endif
#ifdef SIGCONT
    sig_vals[MVM_SIGCONT]   = SIGCONT;
#endif
#ifdef SIGCHLD
    sig_vals[MVM_SIGCHLD]   = SIGCHLD;
#endif
#ifdef SIGTTIN
    sig_vals[MVM_SIGTTIN]   = SIGTTIN;
#endif
#ifdef SIGTTOU
    sig_vals[MVM_SIGTTOU]   = SIGTTOU;
#endif
#ifdef SIGIO
    sig_vals[MVM_SIGIO]     = SIGIO;
#endif
#ifdef SIGXCPU
    sig_vals[MVM_SIGXCPU]   = SIGXCPU;
#endif
#ifdef SIGXFSZ
    sig_vals[MVM_SIGXFSZ]   = SIGXFSZ;
#endif
#ifdef SIGVTALRM
    sig_vals[MVM_SIGVTALRM] = SIGVTALRM;
#endif
#ifdef SIGPROF
    sig_vals[MVM_SIGPROF]   = SIGPROF;
#endif
#ifdef SIGWINCH
    sig_vals[MVM_SIGWINCH]  = SIGWINCH;
#endif
#ifdef SIGINFO
    sig_vals[MVM_SIGINFO]   = SIGINFO;
#endif
#ifdef SIGUSR1
    sig_vals[MVM_SIGUSR1]   = SIGUSR1;
#endif
#ifdef SIGUSR2
    sig_vals[MVM_SIGUSR2]   = SIGUSR2;
#endif
#ifdef SIGTHR
    sig_vals[MVM_SIGTHR]    = SIGTHR;
#endif
#ifdef SIGSTKFLT
    sig_vals[MVM_SIGSTKFLT] = SIGSTKFLT;
#endif
#ifdef SIGPWR
    sig_vals[MVM_SIGPWR]    = SIGPWR;
#endif
#ifdef SIGBREAK
    sig_vals[MVM_SIGBREAK]  = SIGBREAK;
#endif
}

#define SIG_SHIFT(s) (1 << ((s) - 1))

static void populate_instance_valid_sigs(MVMThreadContext *tc, MVMint8 *sig_vals) {
    MVMuint64 valid_sigs = 0;
    MVMint8 i;

    if ( tc->instance->valid_sigs ) return;

    for (i = 0; i < NUM_SIG_WANTED; i++) {
        if (sig_vals[i]) {
            valid_sigs |=  SIG_SHIFT(sig_vals[i]);
        }
    }
    tc->instance->valid_sigs = valid_sigs;
}

MVMObject * MVM_io_get_signals(MVMThreadContext *tc) {
    MVMInstance  * const instance = tc->instance;
    MVMHLLConfig *       hll      = MVM_hll_current(tc);
    MVMObject    *       sig_arr;

    MVMint8 sig_wanted_vals[NUM_SIG_WANTED];
    populate_sig_values(sig_wanted_vals);

    if (instance->sig_arr) {
        return instance->sig_arr;
    }

    sig_arr = MVM_repr_alloc_init(tc, hll->slurpy_array_type);
    MVMROOT(tc, sig_arr) {
        MVMint8 i;
        for (i = 0; i < NUM_SIG_WANTED; i++) {
            MVMObject *key      = NULL;
            MVMString *full_key = NULL;
            MVMObject *val      = NULL;

            MVMROOT3(tc, key, full_key, val) {
                full_key = MVM_string_utf8_c8_decode(
                    tc, instance->VMString, SIG_WANTED[i], strlen(SIG_WANTED[i])
                );

                key = MVM_repr_box_str(tc, hll->str_box_type,
                    MVM_string_substring(tc, full_key, 4, -1)
                );
                val = MVM_repr_box_int(tc, hll->int_box_type, sig_wanted_vals[i]);

                MVM_repr_push_o(tc, sig_arr, key);
                MVM_repr_push_o(tc, sig_arr, val);
            }
        }

        populate_instance_valid_sigs(tc, sig_wanted_vals);
        instance->sig_arr = sig_arr;
    }

    return sig_arr;
}

/* Register a new signal handler. */
MVMObject * MVM_io_signal_handle(
    MVMThreadContext *tc,
    MVMObject *setup_notify_queue, MVMObject *setup_notify_schedulee,
    MVMObject *queue, MVMObject *schedulee,
    MVMint64 signal,
    MVMObject *async_type
) {
    MVMAsyncTask *task;
    SignalInfo   *signal_info;
    MVMInstance  * const instance = tc->instance;

    if ( !instance->valid_sigs ) {
        MVMint8 sig_wanted_vals[NUM_SIG_WANTED];
        populate_sig_values(sig_wanted_vals);
        populate_instance_valid_sigs(tc, sig_wanted_vals);
    }
    if ( signal <= 0 || !(instance->valid_sigs & SIG_SHIFT(signal) ) ) {
        MVM_exception_throw_adhoc(tc, "Unsupported signal handler %d",
            (int)signal);
    }

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "signal target queue must have ConcBlockingQueue REPR");
    if (setup_notify_queue && REPR(setup_notify_queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "signal setup notify queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "signal result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT4(tc, queue, schedulee, setup_notify_queue, setup_notify_schedulee) {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    }
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops      = &op_table;
    signal_info         = MVM_malloc(sizeof(SignalInfo));
    signal_info->signum = signal;
    signal_info->setup_notify_queue = setup_notify_queue;
    signal_info->setup_notify_schedulee = setup_notify_schedulee;
    task->body.data     = signal_info;

    /* Hand the task off to the event loop. */
    MVMROOT(tc, task) {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    }

    return (MVMObject *)task;
}
