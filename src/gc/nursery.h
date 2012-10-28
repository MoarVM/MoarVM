/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 524288

/* Should this thread process permanent roots or not? */
typedef enum {
    MVMGCWhatToDo_All     = 0,
    MVMGCWhatToDo_NoPerms = 1,
    MVMGCWhatToDo_InTray  = 2
} MVMGCWhatToDo;

/* Functions. */
void MVM_gc_nursery_collect(MVMThreadContext *tc, MVMuint8 what_to_do);
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit);
