void MVM_gc_enter_from_allocator(MVMThreadContext *tc);
void MVM_gc_enter_from_interupt(MVMThreadContext *tc);
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc);
void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc);

/* The number of items we must reach in a bucket of work before passing it
 * off to the next thread. (Power of 2, minus 2, is a decent choice.) */
#define MVM_GC_PASS_WORK_SIZE   14

/* Represents a piece of work (some addresses to visit) that have been passed
 * from one thread doing GC to another thread doing GC. */
typedef struct _MVMGCPassedWork {
    MVMCollectable         **items[MVM_GC_PASS_WORK_SIZE];
    MVMint32                 num_items;
    struct _MVMGCPassedWork *next;
} MVMGCPassedWork;
