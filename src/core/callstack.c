#include "moar.h"

/* Allocates a new call stack region, not incorporated into the regions double
 * linked list yet. */
static MVMCallStackRegion * allocate_region(void) {
    MVMCallStackRegion *region = MVM_malloc(MVM_CALLSTACK_REGION_SIZE);
    region->prev = region->next = NULL;
    region->alloc = (char *)region + sizeof(MVMCallStackRegion);
    region->alloc_limit = (char *)region + MVM_CALLSTACK_REGION_SIZE;
    return region;
}

/* Allocates a record in the current call stack region and returns it. Doesn't
 * check if growth is needed. Sets its previous record to the current stack
 * top, but does not itself update the stack top. */
static MVMCallStackRecord * allocate_record_unchecked(MVMThreadContext *tc, MVMuint8 kind, size_t size) {
    MVMCallStackRegion *region = tc->stack_current_region;
    MVMCallStackRecord *record = (MVMCallStackRecord *)region->alloc;
    record->prev = tc->stack_top;
    record->kind = kind;
    region->alloc += size;
    return record;
}

/* Allocates a record, placing it in the current call stack region if possible
 * but moving to the next one if not. Sets its previous record to the current
 * stack top, but does not itself update the stack top. */
static MVMCallStackRecord * allocate_record(MVMThreadContext *tc, MVMuint8 kind, size_t size) {
    MVMCallStackRegion *region = tc->stack_current_region;
    if ((region->alloc_limit - region->alloc) < (ptrdiff_t)size) {
        if (!region->next) {
            MVMCallStackRegion *next = allocate_region();
            region->next = next;
            next->prev = region;
        }
        tc->stack_current_region = region->next;
        tc->stack_top = allocate_record_unchecked(tc, MVM_CALLSTACK_RECORD_START_REGION,
                sizeof(MVMCallStackRegionStart));
    }
    return allocate_record_unchecked(tc, kind, size);
}

/* Called upon thread creation to set up an initial callstack region for the
 * thread. */
void MVM_callstack_init(MVMThreadContext *tc) {
    /* Allocate an initial region, and put a start of stack record in it. */
    tc->stack_first_region = tc->stack_current_region = allocate_region();
    tc->stack_top = allocate_record_unchecked(tc, MVM_CALLSTACK_RECORD_START,
            sizeof(MVMCallStackStart));
}

/* Allocates a bytecode frame record on the callstack. */
MVMCallStackFrame * MVM_callstack_allocate_frame(MVMThreadContext *tc) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_FRAME,
            sizeof(MVMCallStackFrame));
    return (MVMCallStackFrame *)tc->stack_top;
}

/* Allocates a bytecode frame record on the callstack. */
MVMCallStackHeapFrame * MVM_callstack_allocate_heap_frame(MVMThreadContext *tc) {
    tc->stack_top = allocate_record(tc, MVM_CALLSTACK_RECORD_HEAP_FRAME,
            sizeof(MVMCallStackHeapFrame));
    return (MVMCallStackHeapFrame *)tc->stack_top;
}

/* Unwind the calls stack until we reach a prior bytecode frame. */
static int is_bytecode_frame(MVMuint8 kind) {
    switch (kind) {
        case MVM_CALLSTACK_RECORD_FRAME:
        case MVM_CALLSTACK_RECORD_HEAP_FRAME:
        case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
            return 1;
        default:
            return 0;
    }
}
void MVM_callstack_unwind_frame(MVMThreadContext *tc) {
    do {
        /* Do any cleanup actions needed. */
        switch (tc->stack_top->kind) {
            case MVM_CALLSTACK_RECORD_START_REGION:
                tc->stack_current_region = tc->stack_current_region->prev;
                break;
            case MVM_CALLSTACK_RECORD_START:
            case MVM_CALLSTACK_RECORD_FRAME:
            case MVM_CALLSTACK_RECORD_HEAP_FRAME:
            case MVM_CALLSTACK_RECORD_PROMOTED_FRAME:
                /* No cleanup to do. */
                break;
            default:
                MVM_panic(1, "Unknown call stack record type in unwind");
        }
        tc->stack_current_region->alloc = (char *)tc->stack_top;
        tc->stack_top = tc->stack_top->prev;
    } while (tc->stack_top && !is_bytecode_frame(tc->stack_top->kind));
}

/* Called at thread exit to destroy all callstack regions the thread has. */
void MVM_callstack_destroy(MVMThreadContext *tc) {
    MVMCallStackRegion *cur = tc->stack_first_region;
    while (cur) {
        MVMCallStackRegion *next = cur->next;
        MVM_free(cur);
        cur = next;
    }
    tc->stack_first_region = NULL;
}
