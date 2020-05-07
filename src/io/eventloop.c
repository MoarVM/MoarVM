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
static void setup_work(MVMThreadContext *tc) {
    MVMConcBlockingQueue *queue = (MVMConcBlockingQueue *)tc->instance->event_loop_todo_queue;
    MVMObject *task_obj;

    MVMROOT(tc, queue, {
        while (!MVM_is_null(tc, task_obj = MVM_concblockingqueue_poll(tc, queue))) {
            MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
            MVM_ASSERT_NOT_FROMSPACE(tc, task);
            if (task->body.state == MVM_ASYNC_TASK_STATE_NEW) {
                MVMROOT(tc, task, {
                    task->body.ops->setup(tc, tc->instance->event_loop, task_obj, task->body.data);
                    task->body.state = MVM_ASYNC_TASK_STATE_SETUP;
                });
            }
        }
    });
}

/* Performs an async emit permit grant on the loop. */
static void permit_work(MVMThreadContext *tc) {
    MVMConcBlockingQueue *queue = (MVMConcBlockingQueue *)tc->instance->event_loop_permit_queue;
    MVMObject *task_arr;

    MVMROOT(tc, queue, {
        while (!MVM_is_null(tc, task_arr = MVM_concblockingqueue_poll(tc, queue))) {
            MVMObject *task_obj = MVM_repr_at_pos_o(tc, task_arr, 0);
            MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
            MVM_ASSERT_NOT_FROMSPACE(tc, task);
            if (task->body.ops->permit) {
                MVMint64 channel = MVM_repr_get_int(tc, MVM_repr_at_pos_o(tc, task_arr, 1));
                MVMint64 permit = MVM_repr_get_int(tc, MVM_repr_at_pos_o(tc, task_arr, 2));
                task->body.ops->permit(tc, tc->instance->event_loop, task_obj, task->body.data, channel, permit);
            }
        }
    });
}

/* Performs an async cancellation on the loop. */
static void cancel_work(MVMThreadContext *tc) {
    MVMConcBlockingQueue *queue = (MVMConcBlockingQueue *)tc->instance->event_loop_cancel_queue;
    MVMObject *task_obj;

    MVMROOT(tc, queue, {
        while (!MVM_is_null(tc, task_obj = MVM_concblockingqueue_poll(tc, queue))) {
            MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
            MVM_ASSERT_NOT_FROMSPACE(tc, task);
            if (task->body.state == MVM_ASYNC_TASK_STATE_SETUP) {
                MVMROOT(tc, task, {
                    if (task->body.ops->cancel)
                        task->body.ops->cancel(tc, tc->instance->event_loop, task_obj, task->body.data);
                });
            }
            task->body.state = MVM_ASYNC_TASK_STATE_CANCELLED;
        }
    });
}

/* Fired whenever we were signalled that there is a new task or a new
 * cancellation for the event loop to process. */
static void async_handler(uv_async_t *handle) {
    MVMThreadContext *tc = (MVMThreadContext *)handle->data;
    GC_SYNC_POINT(tc);
    setup_work(tc);
    permit_work(tc);
    cancel_work(tc);
}

/* Enters the event loop. */
static void enter_loop(MVMThreadContext *tc, MVMArgs arg_info) {
    uv_loop_t    *loop = tc->instance->event_loop;
    uv_async_t   *async = tc->instance->event_loop_wakeup;

#ifdef MVM_HAS_PTHREAD_SETNAME_NP
    pthread_setname_np(pthread_self(), "async io thread");
#endif

    /* Bind the thread context for the wakeup signal */
    async->data = tc;

    /* Enter event loop */
    uv_run(loop, UV_RUN_DEFAULT);
}

/* Sees if we have an event loop processing thread set up already, and
 * sets it up if not. */
void MVM_io_eventloop_start(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    MVMObject *loop_runner;
    unsigned int interval_id;

    if (instance->event_loop_thread)
        return;

    /* Grab starting mutex and ensure we didn't lose the race. */
    MVM_telemetry_timestamp(tc, "hoping to start an event loop thread");
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(&instance->mutex_event_loop);
    MVM_gc_mark_thread_unblocked(tc);

    interval_id = MVM_telemetry_interval_start(tc, "creating the event loop thread");

    /* We may have lost the race, so we need to setup state carefully */
    /* This may also be present if this is a thread restart */
    if (!instance->event_loop) {
        /* The underlying loop structure that will handle all IO events. */
        instance->event_loop              = MVM_malloc(sizeof(uv_loop_t));
        if (uv_loop_init(instance->event_loop) < 0)
            MVM_panic(1, "Unable to initialize event loop");

        /* The async signal handler for waking up the thread */
        instance->event_loop_wakeup       = MVM_malloc(sizeof(uv_async_t));
        if (uv_async_init(instance->event_loop, instance->event_loop_wakeup, async_handler) != 0)
            MVM_panic(1, "Unable to initialize async wake-up handle for event loop");

        /* Create various bits of state the async event loop thread needs. */
        instance->event_loop_todo_queue   = MVM_repr_alloc_init(tc,
            instance->boot_types.BOOTQueue);
        instance->event_loop_permit_queue = MVM_repr_alloc_init(tc,
            instance->boot_types.BOOTQueue);
        instance->event_loop_cancel_queue = MVM_repr_alloc_init(tc,
            instance->boot_types.BOOTQueue);
        instance->event_loop_active       = MVM_repr_alloc_init(tc,
            instance->boot_types.BOOTArray);
        instance->event_loop_free_indices = MVM_repr_alloc_init(tc,
            instance->boot_types.BOOTIntArray);
    }

    if (!instance->event_loop_thread) {
        /* Start the event loop thread, which will call a C function that
         * sits in the uv loop, never leaving until it is stopped from the
         * outside */
        loop_runner = MVM_repr_alloc_init(tc, instance->boot_types.BOOTCCode);
        ((MVMCFunction *)loop_runner)->body.func = enter_loop;

        instance->event_loop_thread = MVM_thread_new(tc, loop_runner, 1);
        MVM_thread_run(tc, instance->event_loop_thread);
    }

    MVM_telemetry_interval_stop(tc, interval_id, "created the event loop thread");
    uv_mutex_unlock(&instance->mutex_event_loop);
}


/* Adds a work item into the event loop work queue. */
void MVM_io_eventloop_queue_work(MVMThreadContext *tc, MVMObject *work) {
    MVMROOT(tc, work, {
        MVM_io_eventloop_start(tc);
        MVM_repr_push_o(tc, tc->instance->event_loop_todo_queue, work);
        uv_async_send(tc->instance->event_loop_wakeup);
    });
}

/* Permits an asynchronous task to emit more events. This is used to provide a
 * back-pressure mechanism. */
void MVM_io_eventloop_permit(MVMThreadContext *tc, MVMObject *task_obj,
                              MVMint64 channel, MVMint64 permits) {
    if (REPR(task_obj)->ID == MVM_REPR_ID_MVMOSHandle)
        task_obj = MVM_io_get_async_task_handle(tc, task_obj);
    if (REPR(task_obj)->ID == MVM_REPR_ID_MVMAsyncTask) {
        MVMROOT(tc, task_obj, {
            MVMObject *channel_box = NULL;
            MVMObject *permits_box = NULL;
            MVMObject *arr = NULL;
            MVMROOT3(tc, channel_box, permits_box, arr, {
                channel_box = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, channel);
                permits_box = MVM_repr_box_int(tc, tc->instance->boot_types.BOOTInt, permits);
                arr = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTArray);
                MVM_repr_push_o(tc, arr, task_obj);
                MVM_repr_push_o(tc, arr, channel_box);
                MVM_repr_push_o(tc, arr, permits_box);
                MVM_io_eventloop_start(tc);
                MVM_repr_push_o(tc, tc->instance->event_loop_permit_queue, arr);
                uv_async_send(tc->instance->event_loop_wakeup);
            });
        });
    }
    else {
        MVM_exception_throw_adhoc(tc, "Can only permit an AsyncTask handle");
    }
}

/* Cancels a piece of async work. */
void MVM_io_eventloop_cancel_work(MVMThreadContext *tc, MVMObject *task_obj,
        MVMObject *notify_queue, MVMObject *notify_schedulee) {
    if (REPR(task_obj)->ID == MVM_REPR_ID_MVMAsyncTask) {
        if (notify_queue && notify_schedulee) {
            MVMAsyncTask *task = (MVMAsyncTask *)task_obj;
            MVM_ASSIGN_REF(tc, &(task_obj->header), task->body.cancel_notify_queue,
                notify_queue);
            MVM_ASSIGN_REF(tc, &(task_obj->header), task->body.cancel_notify_schedulee,
                notify_schedulee);
        }
        MVMROOT(tc, task_obj, {
            MVM_io_eventloop_start(tc);
            MVM_repr_push_o(tc, tc->instance->event_loop_cancel_queue, task_obj);
            uv_async_send(tc->instance->event_loop_wakeup);
        });
    }
    else {
        MVM_exception_throw_adhoc(tc, "Can only cancel an AsyncTask handle");
    }
}

/* Sends a task cancellation notification if requested for the specified task. */
void MVM_io_eventloop_send_cancellation_notification(MVMThreadContext *tc, MVMAsyncTask *task) {
    MVMObject *notify_queue = task->body.cancel_notify_queue;
    MVMObject *notify_schedulee = task->body.cancel_notify_schedulee;
    if (notify_queue && notify_schedulee)
        MVM_repr_push_o(tc, notify_queue, notify_schedulee);
}

/* Adds a work item to the active async task set. */
int MVM_io_eventloop_add_active_work(MVMThreadContext *tc, MVMObject *async_task) {
    MVMuint64 work_idx = MVM_repr_elems(tc, tc->instance->event_loop_free_indices) > 0
        ? (MVMuint64)MVM_repr_pop_i(tc, tc->instance->event_loop_free_indices)
        : MVM_repr_elems(tc, tc->instance->event_loop_active);
    MVM_ASSERT_NOT_FROMSPACE(tc, async_task);
    MVM_repr_bind_pos_o(tc, tc->instance->event_loop_active, work_idx, async_task);
    return work_idx;
}

/* Gets an active work item from the active work eventloop. */
MVMAsyncTask * MVM_io_eventloop_get_active_work(MVMThreadContext *tc, int work_idx) {
    if (work_idx >= 0 && work_idx < (int)MVM_repr_elems(tc, tc->instance->event_loop_active)) {
        MVMObject *task_obj = MVM_repr_at_pos_o(tc, tc->instance->event_loop_active, work_idx);
        if (REPR(task_obj)->ID != MVM_REPR_ID_MVMAsyncTask)
            MVM_panic(1, "non-AsyncTask fetched from eventloop active work list");
        MVM_ASSERT_NOT_FROMSPACE(tc, task_obj);
        return (MVMAsyncTask *)task_obj;
    }
    else {
        MVM_panic(1, "use of invalid eventloop work item index %d", work_idx);
    }
}

/* Removes an active work index from the active work list, enabling any
 * memory associated with it to be collected. Replaces the work index with -1
 * so that any future use of the task will be a failed lookup. */
void MVM_io_eventloop_remove_active_work(MVMThreadContext *tc, int *work_idx_to_clear) {
    int work_idx = *work_idx_to_clear;
    if (work_idx >= 0 && work_idx < (int)MVM_repr_elems(tc, tc->instance->event_loop_active)) {
        *work_idx_to_clear = -1;
        MVM_repr_bind_pos_o(tc, tc->instance->event_loop_active, work_idx, tc->instance->VMNull);
        MVM_repr_push_i(tc, tc->instance->event_loop_free_indices, work_idx);
    }
    else {
        MVM_panic(1, "cannot remove invalid eventloop work item index %d", work_idx);
    }
}



/* Send the stop signal - no synchronization required */
void MVM_io_eventloop_stop(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    if (!instance->event_loop_thread)
        return;
    /* Stop the loop */
    uv_stop(instance->event_loop);
    uv_async_send(instance->event_loop_wakeup);
}

/* Wait for exit (again, no synchronizaiton required) */
void MVM_io_eventloop_join(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    if (!instance->event_loop_thread)
        return;
    MVM_thread_join(tc, instance->event_loop_thread);
}

/* Clean up used resources. Synchronization required - other threads might modify them as well */
void MVM_io_eventloop_destroy(MVMThreadContext *tc) {
    MVMInstance *instance = tc->instance;
    MVM_gc_mark_thread_blocked(tc);
    uv_mutex_lock(&instance->mutex_event_loop);
    MVM_gc_mark_thread_unblocked(tc);

    if (instance->event_loop_thread) {
        MVM_io_eventloop_stop(tc);
        MVM_io_eventloop_join(tc);
        instance->event_loop_thread = NULL;
    }

    if (instance->event_loop) {
        uv_close((uv_handle_t*)instance->event_loop_wakeup, NULL);

        /* Not sure we can always do this */
        uv_loop_close(instance->event_loop);
       
        MVM_free_null(instance->event_loop_wakeup);
        MVM_free_null(instance->event_loop);
    }

    uv_mutex_unlock(&instance->mutex_event_loop);
}
