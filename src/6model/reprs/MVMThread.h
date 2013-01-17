/* Representation used for VM thread handles. */
typedef struct _MVMThreadBody {
    MVMThreadContext *tc;
    
    /* handle to the invokee of this thread, so that if
     * the GC runs while a thread is starting, it initializes
     * with the correct reference if the code object is moved. */
    MVMObject *invokee;
    
    apr_thread_t *apr_thread;
    apr_pool_t *apr_pool;
    
    /* next in tc's starting_threads list */
    MVMObject *next;
    
    /* flag */
    MVMuint8 started;
} MVMThreadBody;
typedef struct _MVMThread {
    MVMObject common;
    MVMThreadBody body;
} MVMThread;

/* Function for REPR setup. */
MVMREPROps * MVMThread_initialize(MVMThreadContext *tc);
