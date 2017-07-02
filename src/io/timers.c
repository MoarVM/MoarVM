#include "moar.h"

/* Info we convey about a timer. */
typedef struct {
    int timeout;
    int repeat;
    uv_timer_t handle;
    MVMThreadContext *tc;
    int work_idx;
} TimerInfo;

/* Timer callback; dispatches schedulee to the queue. */
static void timer_cb(uv_timer_t *handle) {
    TimerInfo        *ti = (TimerInfo *)handle->data;
    MVMThreadContext *tc = ti->tc;
    MVMAsyncTask     *t  = MVM_io_eventloop_get_active_work(tc, ti->work_idx);
    MVM_repr_push_o(tc, t->body.queue, t->body.schedulee);
}

/* Sets the timer up on the event loop. */
static void setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    TimerInfo *ti = (TimerInfo *)data;
    uv_timer_init(loop, &ti->handle);
    ti->work_idx    = MVM_io_eventloop_add_active_work(tc, async_task);
    ti->tc          = tc;
    ti->handle.data = ti;
    uv_timer_start(&ti->handle, timer_cb, ti->timeout, ti->repeat);
}

/* Stops the timer. */
static void cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    TimerInfo *ti = (TimerInfo *)data;
    uv_timer_stop(&ti->handle);
    MVM_io_eventloop_send_cancellation_notification(ti->tc,
        MVM_io_eventloop_get_active_work(tc, ti->work_idx));
    MVM_io_eventloop_remove_active_work(tc, &(ti->work_idx));
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

/* Creates a new timer. */
MVMObject * MVM_io_timer_create(MVMThreadContext *tc, MVMObject *queue,
                                MVMObject *schedulee, MVMint64 timeout,
                                MVMint64 repeat, MVMObject *async_type) {
    MVMAsyncTask *task;
    TimerInfo *timer_info;

    /* Validate REPRs. */
    if (REPR(queue)->ID != MVM_REPR_ID_ConcBlockingQueue)
        MVM_exception_throw_adhoc(tc,
            "timer target queue must have ConcBlockingQueue REPR");
    if (REPR(async_type)->ID != MVM_REPR_ID_MVMAsyncTask)
        MVM_exception_throw_adhoc(tc,
            "timer result type must have REPR AsyncTask");

    /* Create async task handle. */
    MVMROOT(tc, queue, {
    MVMROOT(tc, schedulee, {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    });
    });
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops      = &op_table;
    timer_info          = MVM_malloc(sizeof(TimerInfo));
    timer_info->timeout = timeout;
    timer_info->repeat  = repeat;
    task->body.data     = timer_info;

    /* Hand the task off to the event loop, which will set up the timer on the
     * event loop. */
    MVMROOT(tc, task, {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    });

    return (MVMObject *)task;
}
