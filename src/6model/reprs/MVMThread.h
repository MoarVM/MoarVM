/* Representation used for VM thread handles. */
typedef enum {
    MVM_thread_stage_starting = 0,
    MVM_thread_stage_waiting = 1,
    MVM_thread_stage_started = 2,
    MVM_thread_stage_exited = 3,
    MVM_thread_stage_clearing_nursery = 4,
    MVM_thread_stage_destroyed = 5
} MVMThreadStages;

typedef struct _MVMThreadBody {
    MVMThreadContext *tc;

    /* handle to the invokee of this thread, so that if
     * the GC runs while a thread is starting, it initializes
     * with the correct reference if the code object is moved. */
    MVMObject *invokee;

    apr_thread_t *apr_thread;
    apr_pool_t *apr_pool;

    /* next in tc's threads list */
    struct _MVMThread *next;

    /* MVMThreadStages */
    AO_t stage;

    /* child currently spawning, so GC can steal it */
    struct _MVMThread *new_child;
} MVMThreadBody;
typedef struct _MVMThread {
    MVMObject common;
    MVMThreadBody body;
} MVMThread;

/* Function for REPR setup. */
MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
