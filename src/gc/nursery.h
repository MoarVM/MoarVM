/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 524288

/* Should this thread process permanent roots or not? */
typedef enum {
    MVMPerms_No  = 0,
    MVMPerms_Yes = 1
} MVMPerms;

/* Functions. */
void MVM_gc_nursery_collect(MVMThreadContext *tc, MVMuint8 process_perms);
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit);
