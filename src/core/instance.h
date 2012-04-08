/* Represents a MoarVM instance. */
typedef struct _MVMInstance {
    /* The list of active threads. */
    MVMThreadContext **threads;
    
    /* The number of active threads. */
    MVMuint16 num_threads;
} MVMInstance;
