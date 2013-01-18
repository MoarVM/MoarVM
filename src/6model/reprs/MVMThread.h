/* Representation used for VM thread handles. */
typedef enum {
    MVM_thread_stage_starting = 0,
    MVM_thread_stage_waiting = 1,
    MVM_thread_stage_started = 2,
    MVM_thread_stage_exited = 3
} MVMThreadStages;

typedef struct _MVMThreadBody {
    MVMThreadContext *tc;
    
    /* handle to the invokee of this thread, so that if
     * the GC runs while a thread is starting, it initializes
     * with the correct reference if the code object is moved. */
    MVMObject *invokee;
    
    apr_thread_t *apr_thread;
    apr_pool_t *apr_pool;
    
    /* next in tc's starting_threads or running_threads list */
    struct _MVMThread *next;
    
    /* MVMThreadStages */
    AO_t stage;
} MVMThreadBody;
typedef struct _MVMThread {
    MVMObject common;
    MVMThreadBody body;
} MVMThread;

/* Function for REPR setup. */
MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
