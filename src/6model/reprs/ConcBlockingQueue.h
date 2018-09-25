/* A single node in the concurrent blocking queue. */
struct MVMConcBlockingQueueNode {
    MVMObject                *value;
    MVMConcBlockingQueueNode *next;
};


/* Representation used for concurrent blocking queue. Rather than hold the body
 * inline, the object holds a pointer to the body. The body itself is allocated
 * by malloc() rather than GC. This prevents it from being moved which would be
 * a problem for mutexes and condition variables. Also, it prevents the GC from
 * moving the body while we are blocked on acquiring a lock (for example) */
struct MVMConcBlockingQueueBody {
    /* Head and tail of the queue. */
    MVMConcBlockingQueueNode *head;
    MVMConcBlockingQueueNode *tail;

    /* Number of elements currently in the queue. */
    AO_t elems;

    /* Locks and condition variables storage. */
    uv_mutex_t  head_lock;
    uv_mutex_t  tail_lock;
    uv_cond_t   head_cond;
};

struct MVMConcBlockingQueue {
    MVMObject common;
    /* As noted, a pointer, not an inline struct */
    MVMConcBlockingQueueBody *body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMConcBlockingQueue_initialize(MVMThreadContext *tc);

/* Operations on concurrent blocking queues. */
MVMObject * MVM_concblockingqueue_poll(MVMThreadContext *tc, MVMConcBlockingQueue *queue);

/* Purely for the convenience of the jit */
MVMObject * MVM_concblockingqueue_jit_poll(MVMThreadContext *tc, MVMObject *queue);
