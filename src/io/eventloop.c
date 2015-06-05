#include "moar.h"

/* Asynchronous I/O, timers, file system notifications and signal handlers
 * have their callbacks processed by this event loop. Its job is mostly to
 * fire off work, receive the callbacks, and put stuff into the concurrent
 * work queue of some scheduler or other. It's backed by a thread that is
 * started in the usual way, but never actually ends up in interpreter;
 * instead, it enters a libuv event loop "forever", until program exit.
 *
 * Work is sent to the event loop by
 */

/* Sets up an async task to be done on the loop. */
static MVMint64 setup_work(MVMThreadContext *tc) {
    MVMConcBlockingQueue *queue = (MVMConcBlockingQueue *)tc->instance->event_loop_todo_queue;
    MVMint64 setup = 0;
    MVMObject *task_obj;

    while (!MVM_is_null(tc, task_obj = MVM_concblockingqueue_poll(tc, queue))) {
        MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
        task->body.ops->setup(tc, tc->loop, task_obj, task->body.data);
        setup = 1;
    }

    return setup;
}

/* Performs an async cancellation on the loop. */
static MVMint64 cancel_work(MVMThreadContext *tc) {
    MVMConcBlockingQueue *queue = (MVMConcBlockingQueue *)tc->instance->event_loop_cancel_queue;
    MVMint64 cancelled = 0;
    MVMObject *task_obj;

    while (!MVM_is_null(tc, task_obj = MVM_concblockingqueue_poll(tc, queue))) {
        MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
        if (task->body.ops->cancel)
            task->body.ops->cancel(tc, tc->loop, task_obj, task->body.data);
        cancelled = 1;
    }

    return cancelled;
}

/* Sees if we have an event loop processing thread set up already, and
 * sets it up if not. */
static void idle_handler(uv_idle_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    GC_SYNC_POINT(tc);
    if (!setup_work(tc) && !cancel_work(tc))
        MVM_thread_yield(tc);
}

static void enter_loop(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    uv_idle_t idle;
    if (uv_idle_init(tc->loop, &idle) != 0)
        MVM_panic(1, "Unable to initialize idle worker for event loop");
    idle.data = tc;
    if (uv_idle_start(&idle, idle_handler) != 0)
        MVM_panic(1, "Unable to start idle worker for event loop");
    uv_run(tc->loop, UV_RUN_DEFAULT);
    MVM_panic(1, "Supposedly unending event loop thread ended");
}

static uv_loop_t *get_or_vivify_loop(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;

    if (!instance->event_loop_thread) {

        /* Grab starting mutex and ensure we didn't lose the race. */
        uv_mutex_lock(&instance->mutex_event_loop_start);

        if (!instance->event_loop_thread) {
            /* Start the event loop thread, which will call a C function that
             * sits in the uv loop, never leaving. */
            MVMObject *thread, *loop_runner;
            instance->event_loop_todo_queue   = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTQueue);
            instance->event_loop_cancel_queue = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTQueue);
            instance->event_loop_active       = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTArray);
            loop_runner = MVM_repr_alloc_init(tc, instance->boot_types.BOOTCCode);
            ((MVMCFunction *)loop_runner)->body.func = enter_loop;
            thread = MVM_thread_new(tc, loop_runner, 1);
            MVM_thread_run(tc, thread);
            instance->event_loop_thread       = ((MVMThread *)thread)->body.tc;
        }

        uv_mutex_unlock(&instance->mutex_event_loop_start);
    }

    return instance->event_loop_thread->loop;
}

/* Adds a work item into the event loop work queue. */
void MVM_io_eventloop_queue_work(MVMThreadContext *tc, MVMObject *work) {
    MVMROOT(tc, work, {
        get_or_vivify_loop(tc);
        MVM_repr_push_o(tc, tc->instance->event_loop_todo_queue, work);
    });
}

/* Cancels a piece of async work. */
void MVM_io_eventloop_cancel_work(MVMThreadContext *tc, MVMObject *task_obj) {
    if (REPR(task_obj)->ID == MVM_REPR_ID_MVMAsyncTask) {
        MVMROOT(tc, task_obj, {
            get_or_vivify_loop(tc);
            MVM_repr_push_o(tc, tc->instance->event_loop_cancel_queue, task_obj);
        });
    }
    else {
        MVM_exception_throw_adhoc(tc, "Can only cancel an AsyncTask handle");
    }
}
