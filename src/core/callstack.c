#include "moar.h"

/* Allocates a new call stack region, not incorporated into the regions double
 * linked list yet. */
static MVMCallStackRegion * create_region() {
    MVMCallStackRegion *region = MVM_malloc(MVM_CALLSTACK_REGIION_SIZE);
    region->prev = region->next = NULL;
    region->alloc = (char *)region + sizeof(MVMCallStackRegion);
    region->alloc_limit = (char *)region + MVM_CALLSTACK_REGIION_SIZE;
    return region;
}

/* Called upon thread creation to set up an initial callstack region for the
 * thread. */
void MVM_callstack_region_init(MVMThreadContext *tc) {
    tc->stack_first = tc->stack_current = create_region();
}

/* Moves the current call stack region we're allocating/freeing in along to
 * the next one in the region chain, allocating that next one if needed. */
MVMCallStackRegion * MVM_callstack_region_next(MVMThreadContext *tc) {
    MVMCallStackRegion *next_region = tc->stack_current->next;
    if (!next_region) {
        next_region = create_region();
        tc->stack_current->next = next_region;
        next_region->prev = tc->stack_current;
    }
    tc->stack_current = next_region;
    return next_region;
}

/* Switches to the previous call stack region, if any. Otherwise, stays in
 * the current region. */
MVMCallStackRegion * MVM_callstack_region_prev(MVMThreadContext *tc) {
    MVMCallStackRegion *prev_region = tc->stack_current->prev;
    if (prev_region)
        tc->stack_current = prev_region;
    else
        prev_region = tc->stack_current;
    return prev_region;
}

/* Resets a threads's callstack to be empty. Used when its contents has been
 * promoted to the heap. */
void MVM_callstack_reset(MVMThreadContext *tc) {
    MVMCallStackRegion *cur_region = tc->stack_current;
    while (cur_region) {
        cur_region->alloc = (char *)cur_region + sizeof(MVMCallStackRegion);
        cur_region = cur_region->prev;
    }
    tc->stack_current = tc->stack_first;
}

/* Called at thread exit to destroy all callstack regions the thread has. */
void MVM_callstack_region_destroy_all(MVMThreadContext *tc) {
    MVMCallStackRegion *cur = tc->stack_first;
    while (cur) {
        MVMCallStackRegion *next = cur->next;
        MVM_free(cur);
        cur = next;
    }
    tc->stack_first = NULL;
}
