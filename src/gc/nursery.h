/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 524288

/* Functions. */
void MVM_gc_nursery_collect(MVMThreadContext *tc);
void MVM_gc_nursery_free_uncopied(MVMThreadContext *tc, void *limit);
