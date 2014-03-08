/* A single node in the concurrent blocking queue. */
struct MVMConcBlockingQueueNode {
    MVMObject                *value;
    MVMConcBlockingQueueNode *next;
};

/* Memory used for mutexes and cond vars; these can't live in the object body
 * directly as they are sensitive to being moved, but putting them together in
 * a single struct means we can malloc a single bit of memory to hold them. */
struct MVMConcBlockingQueueLocks {
    uv_mutex_t  head_lock;
    uv_mutex_t  tail_lock;
    uv_cond_t   head_cond;
};

/* Representation used for concurrent blocking queue. */
struct MVMConcBlockingQueueBody {
    /* Head and tail of the queue. */
    MVMConcBlockingQueueNode *head;
    MVMConcBlockingQueueNode *tail;

    /* Number of elements currently in the queue. */
    AO_t elems;

    /* Locks and condition variables storage. */
    MVMConcBlockingQueueLocks *locks;
};
struct MVMConcBlockingQueue {
    MVMObject common;
    MVMConcBlockingQueueBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMConcBlockingQueue_initialize(MVMThreadContext *tc);

/* Operations on concurrent blocking queues. */
MVMObject * MVM_concblockingqueue_poll(MVMThreadContext *tc, MVMConcBlockingQueue *queue);
