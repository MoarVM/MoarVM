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
    case MVM_SIG_INT:       signum = SIGINT;    break;
#ifdef SIGBREAK
    case MVM_SIG_BREAK:     signum = SIGBREAK;  break;
#endif
    case MVM_SIG_HUP:       signum = SIGHUP;    break;
    case MVM_SIG_WINCH:     signum = SIGWINCH;  break;
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
    MVM_io_eventloop_queue_work(tc, (MVMObject *)task);

    return (MVMObject *)task;
}
