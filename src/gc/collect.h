/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 2097152

/* How often do we collect the second generation? This is specified as the
 * number of nursery runs that happen per full collection. For example, if
 * this is set to 10 then every tenth collection will involve the full heap. */
#define MVM_GC_GEN2_RATIO 10

/* What things should be processed in this GC run? */
typedef enum {
    /* Everything, including the instance-wide roots. If we have many
     * active threads, only one thread will be set to do this. */
    MVMGCWhatToDo_All = 0,

    /* Everything except the instance-wide roots. */
    MVMGCWhatToDo_NoInstance = 1,

    /* Only process the in-tray of work given by other threads. */
    MVMGCWhatToDo_InTray = 2
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
#define MVM_GC_PASS_WORK_SIZE   30

/* Represents a piece of work (some addresses to visit) that have been passed
 * from one thread doing GC to another thread doing GC. */
typedef struct _MVMGCPassedWork {
    MVMCollectable         **items[MVM_GC_PASS_WORK_SIZE];
    struct _MVMGCPassedWork *next;
    struct _MVMGCPassedWork *next_by_sender;
    struct _MVMGCPassedWork *last_by_sender;
    /* XXX see if atomic_or will work on a single flags field */
    AO_t                     completed;
    AO_t                     upvoted;
    MVMint32                 num_items;
} MVMGCPassedWork;

/* Functions. */
void MVM_gc_collect(MVMThreadContext *tc, MVMuint8 what_to_do, MVMuint8 gen);
void MVM_gc_collect_free_nursery_uncopied(MVMThreadContext *tc, void *limit);
void MVM_gc_collect_cleanup_gen2roots(MVMThreadContext *tc);
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *tc);
void MVM_gc_mark_collectable(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable *item);
