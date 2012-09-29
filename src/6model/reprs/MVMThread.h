/* Representation used for VM thread handles. */
typedef struct _MVMThreadBody {
    MVMThreadContext *tc;
    apr_thread_t *apr_thread;
    apr_pool_t *apr_pool;
} MVMThreadBody;
typedef struct _MVMThread {
    MVMObject common;
    MVMThreadBody body;
} MVMThread;

/* Function for REPR setup. */
MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
