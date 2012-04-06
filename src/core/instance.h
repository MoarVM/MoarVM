/* Represents a MoarVM instance. */
typedef struct {
    /* The list of active threads. */
    MVMThreadContext **threads;
    
    /* The number of active threads. */
    uint16 num_threads;
} MVMInstance;
