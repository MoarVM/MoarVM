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
};
struct MVMAsyncTask {
    MVMObject common;
    MVMAsyncTaskBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMAsyncTask_initialize(MVMThreadContext *tc);
