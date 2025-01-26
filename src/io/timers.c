#include "moar.h"

/* Info we convey about a timer. */
typedef struct {
    int timeout;
    int repeat;
    uv_timer_t *handle;
    MVMThreadContext *tc;
    int work_idx;

    /* In order to correct for clock drift in repeating timers, we adjust the
     * repeat interval based on the last time our callback ran. */
    MVMuint64 now_at_prev_callback;
} TimerInfo;

/* Frees the timer's handle memory. */
static void free_timer(uv_handle_t *handle) {
    MVM_free(handle);
}

/* Timer callback; dispatches schedulee to the queue. */
static void timer_cb(uv_timer_t *handle) {
    TimerInfo        *ti = (TimerInfo *)handle->data;
    MVMThreadContext *tc = ti->tc;
    MVMAsyncTask     *t  = MVM_io_eventloop_get_active_work(tc, ti->work_idx);
    MVM_repr_push_o(tc, t->body.queue, t->body.schedulee);
    if (!ti->repeat && ti->work_idx >= 0) {
        /* The timer will only fire once. Having now fired, stop the callback,
         * clean up the handle, and remove the active work so that we will not
         * hold on to the callback and its associated memory. */
        uv_timer_stop(ti->handle);
        uv_close((uv_handle_t *)ti->handle, free_timer);
        MVM_io_eventloop_remove_active_work(tc, &(ti->work_idx));
    }
    else if (ti->repeat) {
        /* To counteract drift, we adjust the time based on the current time. */
        MVMuint64 prev_now = ti->now_at_prev_callback;
        MVMuint64 actually_passed = uv_now(handle->loop) - prev_now;
        MVMint64 diff = actually_passed - ti->repeat;
        if (diff * 2 > ti->repeat) {
            /* We must have missed our tick for some reason.
             * We will just pretend the timing was correct and not
             * change anything. */
            diff = 0;
        }
        else if (diff * -2 < -(MVMint64)ti->repeat) {
            /* From the code in libuv's timer.c I think the timer callback
             * can not be invoked if uv_now is not >= the due time, and the
             * "clamped timeout" that is used is based on timeout + loop->time.
             * In any case, even though this should not be possible, it's
             * probably safe to just do nothing, if it becomes
             * possible later down the line. */
             diff = 0;
        }
        ti->now_at_prev_callback = uv_now(handle->loop) - diff;
        uv_timer_start(handle, timer_cb, ti->repeat - diff, 0);
    }
}

/* Sets the timer up on the event loop. */
static void setup(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    TimerInfo *ti = (TimerInfo *)data;
    ti->handle = MVM_malloc(sizeof(uv_timer_t));
    uv_timer_init(loop, ti->handle);
    ti->work_idx     = MVM_io_eventloop_add_active_work(tc, async_task);
    ti->tc           = tc;
    ti->handle->data = ti;
    /* Instead of libuv's own repeating timer functionality, we take care of
     * restarting the timer on every tick, so that we can correct for drift */
    uv_timer_start(ti->handle, timer_cb, ti->timeout, 0);
}

/* Stops the timer. */
static void cancel(MVMThreadContext *tc, uv_loop_t *loop, MVMObject *async_task, void *data) {
    TimerInfo *ti = (TimerInfo *)data;
    if (ti->work_idx >= 0) {
        uv_timer_stop(ti->handle);
        uv_close((uv_handle_t *)ti->handle, free_timer);
        MVM_io_eventloop_send_cancellation_notification(ti->tc,
            MVM_io_eventloop_get_active_work(tc, ti->work_idx));
        MVM_io_eventloop_remove_active_work(tc, &(ti->work_idx));
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
    MVMROOT2(tc, queue, schedulee) {
        task = (MVMAsyncTask *)MVM_repr_alloc_init(tc, async_type);
    }
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.queue, queue);
    MVM_ASSIGN_REF(tc, &(task->common.header), task->body.schedulee, schedulee);
    task->body.ops      = &op_table;
    timer_info          = MVM_malloc(sizeof(TimerInfo));
    timer_info->timeout = timeout;
    timer_info->repeat  = repeat;
    timer_info->now_at_prev_callback = 0;
    task->body.data     = timer_info;

    /* Hand the task off to the event loop, which will set up the timer on the
     * event loop. */
    MVMROOT(tc, task) {
        MVM_io_eventloop_queue_work(tc, (MVMObject *)task);
    }

    return (MVMObject *)task;
}
