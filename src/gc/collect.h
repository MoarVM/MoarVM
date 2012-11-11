/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 524288

/* How often do we collect the second generation? This is specified as the
 * number of nursery runs that happen per full collection. For example, if
 * this is set to 10 then every tenth collection will involve the full heap. */
#define MVM_GC_GEN2_RATIO 5

/* What things should be processed in this GC run? */
typedef enum {
    /* Everything, including the instance-wide roots. If we have many
     * active threads, only one thread will be set to do this. */
    MVMGCWhatToDo_All = 0,

    /* Everything except the instance-wide roots. */
    MVMGCWhatToDo_NoInstance = 1,
    
    /* Only process the in-tray of work given by other threads. */
    MVMGCWhatToDo_InTray  = 2
} MVMGCWhatToDo;

/* What generation(s) to collect? */
typedef enum {
    /* Only the nursery. */
    MVMGCGenerations_Nursery = 0,
    
    /* Both the nursery and generation 2. */
    MVMGCGenerations_Both = 1
} MVMGCGenerations;

/* Functions. */
void MVM_gc_collect(MVMThreadContext *tc, MVMuint8 what_to_do, MVMuint8 gen);
void MVM_gc_collect_free_nursery_uncopied(MVMThreadContext *tc, void *limit);
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *tc);
