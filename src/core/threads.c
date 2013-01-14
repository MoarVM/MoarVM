#include "moarvm.h"

/* Temporary structure for passing data to thread start. */
struct _MVMThreadStart {
    MVMThreadContext *tc;
    MVMFrame         *caller;
    MVMObject        *invokee;
    MVMCallsite       no_arg_callsite;
};

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation of the thread code. */
static void thread_initial_invoke(MVMThreadContext *tc, void *data) {
    /* The passed data is simply the code object to invoke. */
    struct _MVMThreadStart *ts = (struct _MVMThreadStart *)data;
    
    /* Dummy, 0-arg callsite. */
    ts->no_arg_callsite.arg_flags = NULL;
    ts->no_arg_callsite.arg_count = 0;
    ts->no_arg_callsite.num_pos   = 0;
    
    /* Create initial frame, which sets up all of the interpreter state also. */
    STABLE(ts->invokee)->invoke(tc, ts->invokee, &ts->no_arg_callsite, NULL);
    
    /* This frame should be marked as the thread entry frame, so that any
     * return from it will cause us to drop out of the interpreter and end
     * the thread. */
    tc->thread_entry_frame = tc->cur_frame;
}

/* This callback handles starting execution of a thread. */
static void * APR_THREAD_FUNC start_thread(apr_thread_t *thread, void *data) {
    struct _MVMThreadStart *ts = (struct _MVMThreadStart *)data;
    MVMGCOrchestration *orch;
    
    /* Set the current frame in the thread to be the initial caller;
     * the ref count for this was incremented in the original thread. */
     ts->tc->cur_frame = ts->caller;
     
     /* If we happen to be in a GC run right now, pause until it's done. */
     while ((orch = ts->tc->instance->gc_orch)
            && !orch->finished
            && ts->tc->gc_status != MVMGCStatus_INTERRUPT)
        apr_thread_yield();
    
    /* Enter the interpreter, to run code. */
    MVM_interp_run(ts->tc, &thread_initial_invoke, ts);
    
    /* Now we're done, decrement the reference count of the caller. */
    MVM_frame_dec_ref(ts->tc, ts->caller);

    /* Mark ourselves as blocked, so that another thread will take care
     * of GC-ing our objects. */
    /* XXX This isn't really enough, because it means we never actually
     * free our nursery after the thread ends... */
    MVM_gc_mark_thread_blocked(ts->tc);

    /* Exit the thread, now it's completed. */
    apr_thread_exit(thread, APR_SUCCESS);
    return NULL;
}

/* Adds a thread to the instance's user_threads list. Note that this should only
 * be called by code holding the user_threads mutex. */
static void add_user_threads_entry(MVMInstance *i, MVMThread *thread) {
    i->num_user_threads++;
    i->user_threads = realloc(i->user_threads, i->num_user_threads * sizeof(MVMThread *));
    i->user_threads[i->num_user_threads - 1] = thread;
}

MVMObject * MVM_thread_start(MVMThreadContext *tc, MVMObject *invokee, MVMObject *result_type) {
    int apr_return_status;
    apr_threadattr_t *thread_attr;
    struct _MVMThreadStart *ts;
    MVMObject *child_obj;

    /* Create a thread object to wrap it up in. */
    MVM_gc_root_temp_push(tc, &invokee);
    child_obj = REPR(result_type)->allocate(tc, STABLE(result_type));
    MVM_gc_root_temp_pop();
    if (REPR(child_obj)->ID == MVM_REPR_ID_MVMThread) {
        MVMThread *child = (MVMThread *)child_obj;
        
        /* Create a new thread context and set it up. */
        MVMThreadContext *child_tc = MVM_tc_create(tc->instance);
        child->body.tc = child_tc;
        child_tc->thread_obj = child;

        /* Allocate APR pool for the thread. */
        if ((apr_return_status = apr_pool_create(&child->body.apr_pool, NULL)) != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_threads, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
        }
        
        /* Take the user threads mutex and update global thread-related state,
         * give the thread an ID, etc. */
        if (apr_thread_mutex_lock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_threads, "Unable to lock user_threads mutex");
        child_tc->thread_id = tc->instance->next_user_thread_id;
        tc->instance->next_user_thread_id++;
        add_user_threads_entry(tc->instance, child);
        if (apr_thread_mutex_unlock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_threads, "Unable to unlock user_threads mutex");
        
        /* Create the thread. Note that we take a reference to the current frame,
         * since it must survive to be the dynamic scope of where the thread was
         * started, and there's no promises that the thread won't start before
         * the code creating the thread returns. The count is decremented when
         * the thread is done. */
        ts = malloc(sizeof(struct _MVMThreadStart));
        ts->tc = child_tc;
        ts->caller = MVM_frame_inc_ref(tc, tc->cur_frame);
        ts->invokee = invokee;
        apr_return_status = apr_threadattr_create(&thread_attr, child->body.apr_pool);
        if (apr_return_status != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_compunit, "Could not create threadattr: errorcode %d", apr_return_status);
        }
        apr_return_status = apr_thread_create(&child->body.apr_thread,
            thread_attr, &start_thread, ts, child->body.apr_pool);
        if (apr_return_status != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_compunit, "Could not spawn thread: errorcode %d", apr_return_status);
        }
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
        MVM_gc_mark_thread_blocked(tc);
        apr_return_status = apr_thread_join(&thread_return_status, thread->body.apr_thread);
        MVM_gc_mark_thread_unblocked(tc);
        if (apr_return_status != APR_SUCCESS)
            MVM_panic(MVM_exitcode_compunit, "Could not join thread: errorcode %d", apr_return_status);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Thread handle passed to join must have representation MVMThread");
    }
}
