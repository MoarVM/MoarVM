/* Representation used for VM thread handles. */
typedef enum {
    MVM_thread_stage_starting = 0,
    MVM_thread_stage_waiting = 1,
    MVM_thread_stage_started = 2,
    MVM_thread_stage_exited = 3,
    MVM_thread_stage_clearing_nursery = 4,
    MVM_thread_stage_destroyed = 5
} MVMThreadStages;

struct MVMThreadBody {
    MVMThreadContext *tc;

    /* handle to the invokee of this thread, so that if
     * the GC runs while a thread is starting, it initializes
     * with the correct reference if the code object is moved. */
    MVMObject *invokee;

    uv_thread_t thread;

    /* next in tc's threads list */
    MVMThread *next;

    /* MVMThreadStages */
    AO_t stage;

    /* child currently spawning, so GC can steal it */
    MVMThread *new_child;
};
struct MVMThread {
    MVMObject common;
    MVMThreadBody body;
};

/* Function for REPR setup. */
MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
