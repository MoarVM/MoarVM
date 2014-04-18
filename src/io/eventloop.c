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
MVMint64 setup_work(MVMThreadContext *tc) {
    MVMObject *task_obj;
    MVMint64 setup = 0;
    while ((task_obj = MVM_concblockingqueue_poll(tc,
            (MVMConcBlockingQueue *)tc->instance->event_loop_todo_queue)) != NULL) {
        MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
        task->body.ops->setup(tc, tc->loop, task_obj, task->body.data);
        setup = 1;
    }
    return setup;
}

/* Sees if we have an event loop processing thread set up already, and
 * sets it up if not. */
void idle_handler(uv_idle_t *handle, int status) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    GC_SYNC_POINT(tc);
    setup_work(tc);
}
void enter_loop(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
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
    if (!tc->instance->event_loop_thread) {
        /* Grab starting mutex and ensure we didn't lose the race. */
        uv_mutex_lock(&tc->instance->mutex_event_loop_start);
        if (!tc->instance->event_loop_thread) {
            /* Start the event loop thread, which will call a C function that
             * sits in the uv loop, never leaving. */
            MVMObject *thread, *loop_runner;
            loop_runner = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCCode);
            ((MVMCFunction *)loop_runner)->body.func = enter_loop;
            MVMROOT(tc, loop_runner, {
                thread = MVM_thread_new(tc, loop_runner, 1);
                MVM_thread_run(tc, thread);
            });
            tc->instance->event_loop_thread     = ((MVMThread *)thread)->body.tc;
            tc->instance->event_loop_todo_queue = MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTQueue);
            tc->instance->event_loop_active     = MVM_repr_alloc_init(tc,
                tc->instance->boot_types.BOOTArray);
        }
        uv_mutex_unlock(&tc->instance->mutex_event_loop_start);
    }
    return tc->instance->event_loop_thread->loop;
}

/* Adds a work item into the event loop work queue. */
void MVM_io_eventloop_queue_work(MVMThreadContext *tc, MVMObject *work) {
    MVMROOT(tc, work, {
        uv_loop_t *loop = get_or_vivify_loop(tc);
        MVM_repr_push_o(tc, tc->instance->event_loop_todo_queue, work);
    });
}
