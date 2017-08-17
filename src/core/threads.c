#include "moar.h"
#include <platform/threads.h>

/* Temporary structure for passing data to thread start. */
typedef struct {
    MVMThreadContext *tc;
    MVMObject        *thread_obj;
} ThreadStart;

/* Creates a new thread handle with the MVMThread representation. Does not
 * actually start execution of the thread, but does give it its unique ID. */
MVMObject * MVM_thread_new(MVMThreadContext *tc, MVMObject *invokee, MVMint64 app_lifetime) {
    MVMThread *thread;
    MVMThreadContext *child_tc;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "spawning a new thread off of me");

    /* Create the Thread object and stash code to run and lifetime. */
    MVMROOT(tc, invokee, {
        thread = (MVMThread *)MVM_repr_alloc_init(tc, tc->instance->Thread);
    });
    thread->body.stage = MVM_thread_stage_unstarted;
    MVM_ASSIGN_REF(tc, &(thread->common.header), thread->body.invokee, invokee);
    thread->body.app_lifetime = app_lifetime;

    /* Try to create the new threadcontext. Can throw if libuv can't
     * create a loop for it for some reason (i.e. too many open files) */
    MVMROOT(tc, thread, {
        child_tc = MVM_tc_create(tc, tc->instance);
    });

    /* Set up the new threadcontext a little. */
    child_tc->thread_obj = thread;
    child_tc->thread_id = 1 + MVM_incr(&tc->instance->next_user_thread_id);
        /* Add one, since MVM_incr returns original. */
    thread->body.tc = child_tc;

    MVM_telemetry_interval_stop(child_tc, interval_id, "i'm the newly spawned thread.");

    /* Also make a copy of the thread ID in the thread object itself, so it
     * is available once the thread dies and its ThreadContext is gone. */
    thread->body.thread_id = child_tc->thread_id;

    return (MVMObject *)thread;
}

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation of the thread code. */
static void thread_initial_invoke(MVMThreadContext *tc, void *data) {
    /* The passed data is simply the code object to invoke. */
    ThreadStart *ts = (ThreadStart *)data;
    MVMThread *thread = (MVMThread *)ts->thread_obj;
    MVMObject *invokee = thread->body.invokee;
    thread->body.invokee = NULL;

    /* Create initial frame, which sets up all of the interpreter state also. */
    invokee = MVM_frame_find_invokee(tc, invokee, NULL);
    STABLE(invokee)->invoke(tc, invokee, MVM_callsite_get_common(tc, MVM_CALLSITE_ID_NULL_ARGS), NULL);

    /* This frame should be marked as the thread entry frame, so that any
     * return from it will cause us to drop out of the interpreter and end
     * the thread. */
    tc->thread_entry_frame = tc->cur_frame;
}

/* This callback handles starting execution of a thread. */
static void start_thread(void *data) {
    ThreadStart *ts = (ThreadStart *)data;
    MVMThreadContext *tc = ts->tc;

    /* Stash thread ID. */
    tc->thread_obj->body.native_thread_id = MVM_platform_thread_id();

    /* wait for the GC to finish if it's not finished stealing us. */
    MVM_gc_mark_thread_unblocked(tc);
    tc->thread_obj->body.stage = MVM_thread_stage_started;

    /* Create a spesh log for this thread. */
    MVM_spesh_log_initialize_thread(tc, 0);

    /* Enter the interpreter, to run code. */
    MVM_interp_run(tc, thread_initial_invoke, ts);

    /* Pop the temp root stack's ts->thread_obj, if it's still there (if we
     * cleared the temp root stack on exception at some point, it'll already be
     * gone). */
    if (tc->num_temproots != 0)
        MVM_gc_root_temp_pop_n(tc, tc->num_temproots);
    MVM_free(ts);

    /* Mark as exited, so the GC will know to clear our stuff. */
    tc->thread_obj->body.stage = MVM_thread_stage_exited;

    /* Mark ourselves as blocked, so that another thread will take care
     * of GC-ing our objects and cleaning up our thread context. */
    MVM_gc_mark_thread_blocked(tc);

    /* Exit the thread, now it's completed. */
    MVM_platform_thread_exit(NULL);
}

/* Begins execution of a thread. */
void MVM_thread_run(MVMThreadContext *tc, MVMObject *thread_obj) {
    MVMThread *child = (MVMThread *)thread_obj;
    int status, added;
    ThreadStart *ts;

    if (REPR(child)->ID == MVM_REPR_ID_MVMThread) {
        MVMThreadContext *child_tc = child->body.tc;

        /* Mark thread as GC blocked until the thread actually starts. */
        MVM_gc_mark_thread_blocked(child_tc);

        /* Create thread state, to pass to the thread start callback. */
        ts = MVM_malloc(sizeof(ThreadStart));
        ts->tc = child_tc;

        /* Push to starting threads list. We may need to retry this if we are
         * asked to join a GC run at this point (since the GC would already
         * have taken a snapshot of the thread list, so it's not safe to add
         * another at this point). */
        added = 0;
        while (!added) {
            uv_mutex_lock(&tc->instance->mutex_threads);
            if (MVM_load(&tc->gc_status) == MVMGCStatus_NONE) {
                /* Insert into list. */
                MVM_ASSIGN_REF(tc, &(child->common.header), child->body.next,
                    tc->instance->threads);
                tc->instance->threads = child;

                /* Store the thread object in the thread start information and
                 * keep it alive by putting it in the *child* tc's temp roots. */
                ts->thread_obj = thread_obj;
                MVM_gc_root_temp_push(child_tc, (MVMCollectable **)&ts->thread_obj);

                /* Move thread to starting stage. */
                child->body.stage = MVM_thread_stage_starting;

                /* Mark us done and unlock the mutex; any GC run will now have
                 * a consistent view of the thread list and can safely run. */
                added = 1;
                uv_mutex_unlock(&tc->instance->mutex_threads);
            }
            else {
                /* Another thread decided we'll GC now. Release mutex, and
                 * do the GC, making sure thread_obj and child are marked. */
                uv_mutex_unlock(&tc->instance->mutex_threads);
                MVMROOT(tc, thread_obj, {
                MVMROOT(tc, child, {
                    GC_SYNC_POINT(tc);
                });
                });
            }
        }

        /* Do the actual thread creation. */
        status = uv_thread_create(&child->body.thread, start_thread, ts);
        if (status < 0)
            MVM_panic(MVM_exitcode_compunit, "Could not spawn thread: errorcode %d", status);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to run must have representation MVMThread");
    }
}

/* Waits for a thread to finish. */
static int try_join(MVMThreadContext *tc, MVMThread *thread) {
    /* Join the thread, marking ourselves as unable to GC while we wait. */
    int status;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&thread);
    MVM_gc_mark_thread_blocked(tc);
    if (thread->body.stage < MVM_thread_stage_exited) {
        status = uv_thread_join(&thread->body.thread);
    }
    else {
        /* the target already ended */
        status = 0;
    }
    MVM_gc_mark_thread_unblocked(tc);
    MVM_gc_root_temp_pop(tc);

    /* After a thread has been joined, we trigger a GC run to clean up after
     * it. This avoids problems where a program spawns threads and joins them
     * in a loop gobbling a load of memory and other resources because we do
     * not ever trigger a GC run to clean up the thread. */
    MVM_gc_enter_from_allocator(tc);

    return status;
}
void MVM_thread_join(MVMThreadContext *tc, MVMObject *thread_obj) {
    if (REPR(thread_obj)->ID == MVM_REPR_ID_MVMThread) {
        int status = try_join(tc, (MVMThread *)thread_obj);
        if (status < 0)
            MVM_panic(MVM_exitcode_compunit, "Could not join thread: errorcode %d", status);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to join must have representation MVMThread");
    }
}

/* Gets the (VM-level) ID of a thread. */
MVMint64 MVM_thread_id(MVMThreadContext *tc, MVMObject *thread_obj) {
    if (REPR(thread_obj)->ID == MVM_REPR_ID_MVMThread)
        return ((MVMThread *)thread_obj)->body.thread_id;
    else
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to threadid must have representation MVMThread");
}

/* Gets the native OS ID of a thread. If it's not yet available because
 * the thread was not yet started, this will return 0. */
MVMint64 MVM_thread_native_id(MVMThreadContext *tc, MVMObject *thread_obj) {
    if (REPR(thread_obj)->ID == MVM_REPR_ID_MVMThread)
        return ((MVMThread *)thread_obj)->body.native_thread_id;
    else
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to threadnativeid must have representation MVMThread");
}

/* Yields control to another thread. */
void MVM_thread_yield(MVMThreadContext *tc) {
    MVM_telemetry_timestamp(tc, "thread yielding");
    MVM_platform_thread_yield();
}

/* Gets the object representing the current thread. */
MVMObject * MVM_thread_current(MVMThreadContext *tc) {
    return (MVMObject *)tc->thread_obj;
}

void MVM_thread_cleanup_threads_list(MVMThreadContext *tc, MVMThread **head) {
    /* Assumed to be the only thread accessing the list.
     * must set next on every item. */
    MVMThread *new_list = NULL, *this = *head, *next;
    *head = NULL;
    while (this) {
        next = this->body.next;
        switch (this->body.stage) {
            case MVM_thread_stage_starting:
            case MVM_thread_stage_waiting:
            case MVM_thread_stage_started:
            case MVM_thread_stage_exited:
            case MVM_thread_stage_clearing_nursery:
                /* push it to the new starting list */
                this->body.next = new_list;
                new_list = this;
                break;
            case MVM_thread_stage_destroyed:
                /* don't put in a list */
                this->body.next = NULL;
                break;
            default:
                MVM_panic(MVM_exitcode_threads, "Thread in unknown stage: %"MVM_PRSz"\n", this->body.stage);
        }
        this = next;
    }
    *head = new_list;
}

/* Goes through all non-app-lifetime threads and joins them. */
void MVM_thread_join_foreground(MVMThreadContext *tc) {
    MVMint64 work = 1;
    while (work) {
        MVMThread *cur_thread = tc->instance->threads;
        work = 0;
        while (cur_thread) {
            if (cur_thread->body.tc != tc->instance->main_thread) {
                if (!cur_thread->body.app_lifetime) {
                    if (MVM_load(&cur_thread->body.stage) < MVM_thread_stage_exited) {
                        /* Join may trigger GC and invalidate cur_thread, so we
                        * just set work to 1 and do another trip around the main
                        * loop. */
                        try_join(tc, cur_thread);
                        work = 1;
                        break;
                    }
                }
            }
            cur_thread = cur_thread->body.next;
        }
    }
}

