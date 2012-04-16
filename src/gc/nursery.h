/* How big is the nursery area? Note that since it's semi-space copying, we
 * actually have double this amount allocated. Also it is per thread. */
#define MVM_NURSERY_SIZE 524288
