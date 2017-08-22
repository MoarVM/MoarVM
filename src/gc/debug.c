#include "moar.h"

#if MVM_GC_DEBUG
/* Takes a pointer of a GC-allocated object, and goes through all of the
 * alive thread's fromspace/tospace regions and all gen2 pages to try and
 * find the region the pointer is part of. */
void MVM_gc_debug_find_region(MVMThreadContext *tc, void *ptr) {
    MVMThread *cur_thread = tc->instance->threads;
    while (cur_thread) {
        MVMThreadContext *thread_tc = cur_thread->body.tc;
        if (thread_tc) {
            if (ptr >= thread_tc->nursery_fromspace &&
                    (char *)ptr < (char *)thread_tc->nursery_fromspace + MVM_NURSERY_SIZE) {
                printf("In fromspace of thread %d\n", cur_thread->body.thread_id);
                return;
            }
            if (ptr >= thread_tc->nursery_tospace &&
                    (char *)ptr < (char *)thread_tc->nursery_tospace + MVM_NURSERY_SIZE) {
                printf("In tospace of thread %d\n", cur_thread->body.thread_id);
                return;
            }
            if (thread_tc->gen2) {
                MVMGen2Allocator *gen2 = thread_tc->gen2;
                MVMint32 bin;
                for (bin = 0; bin < MVM_GEN2_BINS; bin++) {
                    MVMGen2SizeClass *szc = &(gen2->size_classes[bin]);
                    MVMint32 page;
                    for (page = 0; page < szc->num_pages; page++) {
                        char *page_start = szc->pages[page];
                        size_t page_size = MVM_GEN2_PAGE_ITEMS * ((bin + 1) << MVM_GEN2_BIN_BITS);
                        char *page_end = page_start + page_size;
                        if (ptr >= (void*)page_start && ptr < (void*)page_end) {
                            printf("In gen2 bin of thread %d\n", cur_thread->body.thread_id);
                            return;
                        }
                    }
                }
            }
        }
        cur_thread = cur_thread->body.next;
    }
    printf("Not found\n");
}
#endif
