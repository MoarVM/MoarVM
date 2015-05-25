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

/* Fired whenever we were signalled that there is a new task or a new
 * cancellation for the event loop to process. */
static void async_handler(uv_async_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    GC_SYNC_POINT(tc);
    setup_work(tc);
    cancel_work(tc);
}

/* Periodic "do we need to join in with GC" check. */
static void gc_handler(uv_timer_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    GC_SYNC_POINT(tc);
}

/* Prepare/check handlers for GC integration (see explanation in enter_loop). */
static void prepare_handler(uv_prepare_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    MVM_gc_mark_thread_blocked(tc);
}
static void check_handler(uv_check_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    MVM_gc_mark_thread_unblocked(tc);
}

/* Enters the event loop. */
static void enter_loop(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    uv_async_t   *async;
    uv_prepare_t  prep;
    uv_check_t    check;
    int           r;

    /* When the event loop goes to wait for I/O, timers, etc. then it calls
     * prepare. During this time we mark the thread unable to GC, so that if a
     * GC is triggered then it will be work-stolen. The check callback happens
     * after we finish waiting, and can potentially do a GC again. */
    if ((r = uv_prepare_init(tc->loop, &prep)) < 0)
        MVM_exception_throw_adhoc(tc,
            "Failed to initialize event loop prepare handle: %s", uv_strerror(r));
    if ((r = uv_check_init(tc->loop, &check)) < 0)
        MVM_exception_throw_adhoc(tc,
            "Failed to initialize event loop check handle: %s", uv_strerror(r));
    prep.data = tc;
    check.data = tc;
    if ((r = uv_prepare_start(&prep, prepare_handler)) < 0)
        MVM_exception_throw_adhoc(tc,
            "Failed to start event loop prepare handle: %s", uv_strerror(r));
    if ((r = uv_check_start(&check, check_handler)) < 0)
        MVM_exception_throw_adhoc(tc,
            "Failed to start event loop check handle: %s", uv_strerror(r));

    /* Set up async handler so we can be woken up when there's new tasks. */
    async = MVM_malloc(sizeof(uv_async_t));
    if (uv_async_init(tc->loop, async, async_handler) != 0)
        MVM_panic(1, "Unable to initialize async wake-up handle for event loop");
    async->data = tc;
    tc->instance->event_loop_wakeup = async;

    /* Signal that the event loop is ready for processing. */
    uv_sem_post(&(tc->instance->sem_event_loop_started));

    /* Enter event loop; should never leave it. */
    uv_run(tc->loop, UV_RUN_DEFAULT);
    MVM_panic(1, "Supposedly unending event loop thread ended");
}

/* Sees if we have an event loop processing thread set up already, and
 * sets it up if not. */
static uv_loop_t *get_or_vivify_loop(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;

    if (!instance->event_loop_thread) {
        /* Grab starting mutex and ensure we didn't lose the race. */
        uv_mutex_lock(&instance->mutex_event_loop_start);
        if (!instance->event_loop_thread) {
            MVMObject *thread, *loop_runner;
            int r;

            /* Create various bits of state the async event loop thread needs. */
            instance->event_loop_todo_queue   = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTQueue);
            instance->event_loop_cancel_queue = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTQueue);
            instance->event_loop_active       = MVM_repr_alloc_init(tc,
                instance->boot_types.BOOTArray);

            /* We need to wait until we know the event loop has started; we'll
             * use a semaphore for this purpose. */
            if ((r = uv_sem_init(&(instance->sem_event_loop_started), 0)) < 0) {
                uv_mutex_unlock(&instance->mutex_event_loop_start);
                MVM_exception_throw_adhoc(tc, "Failed to initialize event loop start semaphore: %s",
                    uv_strerror(r));
            }

            /* Start the event loop thread, which will call a C function that
             * sits in the uv loop, never leaving. */
            loop_runner = MVM_repr_alloc_init(tc, instance->boot_types.BOOTCCode);
            ((MVMCFunction *)loop_runner)->body.func = enter_loop;
            thread = MVM_thread_new(tc, loop_runner, 1);
            MVMROOT(tc, thread, {
                MVM_thread_run(tc, thread);

                /* Block until we know it's fully started and initialized. */
                uv_sem_wait(&(instance->sem_event_loop_started));
                uv_sem_destroy(&(instance->sem_event_loop_started));

                /* Make the started event loop thread visible to others. */
                instance->event_loop_thread = ((MVMThread *)thread)->body.tc;
            });
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
        uv_async_send(tc->instance->event_loop_wakeup);
    });
}

/* Cancels a piece of async work. */
void MVM_io_eventloop_cancel_work(MVMThreadContext *tc, MVMObject *task_obj) {
    if (REPR(task_obj)->ID == MVM_REPR_ID_MVMAsyncTask) {
        MVMROOT(tc, task_obj, {
            get_or_vivify_loop(tc);
            MVM_repr_push_o(tc, tc->instance->event_loop_cancel_queue, task_obj);
            uv_async_send(tc->instance->event_loop_wakeup);
        });
    }
    else {
        MVM_exception_throw_adhoc(tc, "Can only cancel an AsyncTask handle");
    }
}
