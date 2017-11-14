/* States an async task can be in. */
#define MVM_ASYNC_TASK_STATE_NEW        0
#define MVM_ASYNC_TASK_STATE_SETUP      1
#define MVM_ASYNC_TASK_STATE_CANCELLED  2

/* Representation serving as a handle to an asynchronous task. */
struct MVMAsyncTaskBody {
    /* The queue to schedule a result handler on. */
    MVMObject *queue;

    /* The result handler to schedule. */
    MVMObject *schedulee;

    /* Async task operation table. */
    const MVMAsyncTaskOps *ops;

    /* Data stored by operation type. */
    void *data;

    /* The queue to schedule a cancellation notification on, if any. */
    MVMObject *cancel_notify_queue;

    /* The cancellation notification handler, if any. */
    MVMObject *cancel_notify_schedulee;

    /* The current state of the task. */
    MVMint32 state;
};
struct MVMAsyncTask {
    MVMObject common;
    MVMAsyncTaskBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMAsyncTask_initialize(MVMThreadContext *tc);
