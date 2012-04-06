/* Possible values for the thread execution interupt flag. */
typedef enum {
    /* No interuption needed, continue execution. */
    MVMInterupt_NONE   = 0,

    /* Stop and do a GC scan. */
    MVMInterupt_GCSCAN = 1
} MVMInteruptType;

/* Information associated with an executing thread. */
typedef struct {
    /* Execution interupt flag. */
    MVMInteruptType interupt;
    
    /* Internal ID of the thread. */
    uint16 thread_id;
    
    /* The VM instance that this thread belongs to. */
    MVMInstance *instance;
    
    /* The current allocation pointer, where the next object to be allocated
     * should be placed. */
    void *alloc;
    
    /* The end of the space we're allowed to allocate to. */
    void *alloc_limit;
    
    /* Start of the mutator's thread-local allocation space; put another way,
     * the current nursery. */
    void *fromspace;
    
    /* Where we evacuate objects to when collection this thread's nursery. */
    void *tospace;
    
    /* OS thread handle. */
    void *os_thread; /* XXX Something better than void *... */
} MVMThreadContext;
