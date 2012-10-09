#include "moarvm.h"

/* Temporary structure for passing data to thread start. */
struct _MVMThreadStart {
    MVMThreadContext *tc;
    MVMObject        *invokee;
};

/* This callback is passed to the interpreter code. It takes care of making
 * the initial invocation of the thread code. */
static void thread_initial_invoke(MVMThreadContext *tc, void *data) {
    /* The passed data is simply the code object to invoke. */
    MVMObject *code = (MVMObject *)data;
    
    /* Dummy, 0-arg callsite. */
    MVMCallsite no_arg_callsite;
    no_arg_callsite.arg_flags = NULL;
    no_arg_callsite.arg_count = 0;
    no_arg_callsite.num_pos   = 0;
    
    /* Create initial frame, which sets up all of the interpreter state also. */
    STABLE(code)->invoke(tc, code, &no_arg_callsite, NULL);
}

/* This callback handles starting execution of a thread. It invokes
 * the passed code object using the passed thread context. */
static void * APR_THREAD_FUNC start_thread(apr_thread_t *thread, void *data) {
    struct _MVMThreadStart *ts = (struct _MVMThreadStart *)data;
    MVM_interp_run(ts->tc, &thread_initial_invoke, ts->invokee);
    return NULL;
}

MVMObject * MVM_thread_start(MVMThreadContext *tc, MVMObject *invokee, MVMObject *result_type) {
    int apr_return_status;
    apr_threadattr_t *thread_attr;
    struct _MVMThreadStart *ts;
    MVMObject *child_obj;

    /* Create a thread object to wrap it up in. */
    child_obj = REPR(result_type)->allocate(tc, STABLE(result_type));
    if (REPR(child_obj)->ID == MVM_REPR_ID_MVMThread) {
        MVMThread *child = (MVMThread *)child_obj;
        
        /* Create a new thread context. */
        MVMThreadContext *child_tc = MVM_tc_create(tc->instance);
        child->body.tc = child_tc;
        
        /* Allocate APR pool. */
        if ((apr_return_status = apr_pool_create(&child->body.apr_pool, NULL)) != APR_SUCCESS) {
            MVM_panic(MVM_exitcode_compunit, "Could not allocate APR memory pool: errorcode %d", apr_return_status);
        }
        
        /* Create the thread. */
        ts = malloc(sizeof(struct _MVMThreadStart));
        ts->tc = child_tc;
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
