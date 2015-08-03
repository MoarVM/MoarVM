/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. (In
 * the future, we'll make this adaptive rather than a constant.) */
#define MVM_NURSERY_SIZE 4194304

/* How many bytes should have been promoted into gen2 before we decide to
 * do a full GC run? The numbers below are used as a base amount plus an
 * extra amount per extra thread we have running. */
#define MVM_GC_GEN2_THRESHOLD_BASE      (30 * 1024 * 1024)
#define MVM_GC_GEN2_THRESHOLD_THREAD    (2 * 1024 * 1024)

/* What things should be processed in this GC run? */
typedef enum {
    /* Everything, including the instance-wide roots. If we have many
     * active threads, only one thread will be set to do this. */
    MVMGCWhatToDo_All = 0,

    /* Everything except the instance-wide roots. */
    MVMGCWhatToDo_NoInstance = 1,

    /* Only process the in-tray of work given by other threads. */
    MVMGCWhatToDo_InTray = 2,

    /* Only process the finalizing list. */
    MVMGCWhatToDo_Finalizing = 4
} MVMGCWhatToDo;

/* What generation(s) to collect? */
typedef enum {
    /* Only the nursery. */
    MVMGCGenerations_Nursery = 0,

    /* Both the nursery and generation 2. */
    MVMGCGenerations_Both = 1
} MVMGCGenerations;

/* The number of items we must reach in a bucket of work before passing it
 * off to the next thread. (Power of 2, minus 2, is a decent choice.) */
#define MVM_GC_PASS_WORK_SIZE   62

/* Represents a piece of work (some addresses to visit) that have been passed
 * from one thread doing GC to another thread doing GC. */
struct MVMGCPassedWork {
    MVMCollectable **items[MVM_GC_PASS_WORK_SIZE];
    MVMGCPassedWork *next;
    MVMint32         num_items;
};

/* Functions. */
void MVM_gc_collect(MVMThreadContext *tc, MVMuint8 what_to_do, MVMuint8 gen);
void MVM_gc_collect_free_nursery_uncopied(MVMThreadContext *tc, void *limit);
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *tc, MVMint32 global_destruction);
void MVM_gc_mark_collectable(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable *item);
void MVM_gc_collect_free_stables(MVMThreadContext *tc);
