#include "moarvm.h"

/* Temporary structure for passing data to thread start. */
struct _MVMThreadStart {
    MVMThreadContext *tc;
    MVMFrame         *caller;
    /* was formerly the MVMCode invokee representing the object, but now
     * it is the MVMThread (which in turns has a handle to the invokee). */
    MVMObject        *thread_obj;
    MVMCallsite       no_arg_callsite;
};

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation of the thread code. */
static void thread_initial_invoke(MVMThreadContext *tc, void *data) {
    /* The passed data is simply the code object to invoke. */
    struct _MVMThreadStart *ts = (struct _MVMThreadStart *)data;
    MVMThread *thread = (MVMThread *)ts->thread_obj;
    MVMObject *invokee = thread->body.invokee;

    thread->body.invokee = NULL;

    /* Dummy, 0-arg callsite. */
    ts->no_arg_callsite.arg_flags = NULL;
    ts->no_arg_callsite.arg_count = 0;
    ts->no_arg_callsite.num_pos   = 0;

    /* Create initial frame, which sets up all of the interpreter state also. */
    STABLE(invokee)->invoke(tc, invokee, &ts->no_arg_callsite, NULL);

    /* This frame should be marked as the thread entry frame, so that any
     * return from it will cause us to drop out of the interpreter and end
     * the thread. */
    tc->thread_entry_frame = tc->cur_frame;
}

/* This callback handles starting execution of a thread. */
static void * APR_THREAD_FUNC start_thread(apr_thread_t *thread, void *data) {
    struct _MVMThreadStart *ts = (struct _MVMThreadStart *)data;
    MVMThreadContext *tc = ts->tc;

    /* Set the current frame in the thread to be the initial caller;
     * the ref count for this was incremented in the original thread. */
    tc->cur_frame = ts->caller;

    /* wait for the GC to finish if it's not finished stealing us. */
    MVM_gc_mark_thread_unblocked(tc);
    tc->thread_obj->body.stage = MVM_thread_stage_started;

    /* Enter the interpreter, to run code. */
    MVM_interp_run(tc, &thread_initial_invoke, ts);

    /* mark as exited, so the GC will know to clear our stuff. */
    tc->thread_obj->body.stage = MVM_thread_stage_exited;

    /* Now we're done, decrement the reference count of the caller. */
    MVM_frame_dec_ref(tc, ts->caller);

    /* Mark ourselves as dying, so that another thread will take care
     * of GC-ing our objects and cleaning up our thread context. */
    MVM_gc_mark_thread_blocked(tc);

    /* these are about to destroy themselves */
    tc->thread_obj->body.apr_thread = NULL;
    tc->thread_obj->body.apr_pool = NULL;
    /* hopefully pop the ts->thread_obj temp */
    MVM_gc_root_temp_pop(tc);
    free(ts);

    /* Exit the thread, now it's completed. */
    apr_thread_exit(thread, APR_SUCCESS);

    return NULL;
}

MVMObject * MVM_thread_start(MVMThreadContext *tc, MVMObject *invokee, MVMObject *result_type) {
    int apr_return_status;
    apr_threadattr_t *thread_attr;
    struct _MVMThreadStart *ts;
    MVMObject *child_obj;

    /* Create a thread object to wrap it up in. */
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&invokee);
    child_obj = REPR(result_type)->allocate(tc, STABLE(result_type));
    MVM_gc_root_temp_pop(tc);
    if (REPR(child_obj)->ID == MVM_REPR_ID_MVMThread) {
        MVMThread *child = (MVMThread *)child_obj;
        MVMThread * volatile *threads;

        /* Create a new thread context and set it up. */
        MVMThreadContext *child_tc = MVM_tc_create(tc->instance);
        child->body.tc = child_tc;
        MVM_ASSIGN_REF(tc, child, child->body.invokee, invokee);
        child_tc->thread_obj = child;
        child_tc->thread_id = MVM_atomic_incr(&tc->instance->next_user_thread_id);

        /* Allocate APR pool for the thread. */
        if ((apr_return_status = apr_pool_create(&child->body.apr_pool, NULL)) != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_threads, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
        }

        /* Create the thread. Note that we take a reference to the current frame,
         * since it must survive to be the dynamic scope of where the thread was
         * started, and there's no promises that the thread won't start before
         * the code creating the thread returns. The count is decremented when
         * the thread is done. */
        ts = malloc(sizeof(struct _MVMThreadStart));
        ts->tc = child_tc;
        ts->caller = MVM_frame_inc_ref(tc, tc->cur_frame);
        ts->thread_obj = child_obj;

        /* push this to the *child* tc's temp roots. */
        MVM_gc_root_temp_push(child_tc, (MVMCollectable **)&ts->thread_obj);

        /* Signal to the GC we have a childbirth in progress. The GC
         * will null it for us. */
        MVM_gc_mark_thread_blocked(child_tc);
        MVM_ASSIGN_REF(tc, tc->thread_obj, tc->thread_obj->body.new_child, child);

        /* push to starting threads list */
        threads = &tc->instance->threads;
        do {
            MVMThread *curr = *threads;
            MVM_ASSIGN_REF(tc, child, child->body.next, curr);
        } while (apr_atomic_casptr((volatile void**)threads, child, child->body.next) != child->body.next);

        apr_return_status = apr_threadattr_create(&thread_attr, child->body.apr_pool);
        if (apr_return_status != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_compunit, "Could not create threadattr: errorcode %d", apr_return_status);
        }

        apr_return_status = apr_thread_create(&child->body.apr_thread,
            thread_attr, &start_thread, ts, child->body.apr_pool);

        if (apr_return_status != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_compunit, "Could not spawn thread: errorcode %d", apr_return_status);
        }

        /* need to run the GC to clear our new_child field in case we try
         * try to launch another thread before the GC runs and before the
         * thread starts. */
        GC_SYNC_POINT(tc);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Thread result type must have representation MVMThread");
    }

    return child_obj;
}

void MVM_thread_join(MVMThreadContext *tc, MVMObject *thread_obj) {
    if (REPR(thread_obj)->ID == MVM_REPR_ID_MVMThread) {
        MVMThread *thread = (MVMThread *)thread_obj;
        apr_status_t thread_return_status, apr_return_status;
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&thread);
        MVM_gc_mark_thread_blocked(tc);
        if (((MVMThread *)thread_obj)->body.stage < MVM_thread_stage_exited) {
            apr_return_status = apr_thread_join(&thread_return_status, thread->body.apr_thread);
        }
        else { /* the target already ended */
            apr_return_status = APR_SUCCESS;
        }
        MVM_gc_mark_thread_unblocked(tc);
        MVM_gc_root_temp_pop(tc);
        if (apr_return_status != APR_SUCCESS)
            MVM_panic(MVM_exitcode_compunit, "Could not join thread: errorcode %d", apr_return_status);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to join must have representation MVMThread");
    }
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
                MVM_panic(MVM_exitcode_threads, "Thread in unknown stage: %d\n", this->body.stage);
        }
        this = next;
    }
    *head = new_list;
}
