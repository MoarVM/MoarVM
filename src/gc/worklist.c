#include "moar.h"

/* Allocates a new GC worklist. */
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc, MVMuint8 include_gen2) {
    MVMGCWorklist *worklist = MVM_malloc(sizeof(MVMGCWorklist));
    worklist->items = 0;
    worklist->alloc = MVM_GC_WORKLIST_START_SIZE;
    worklist->list  = MVM_malloc(worklist->alloc * sizeof(MVMCollectable **));
    worklist->include_gen2 = include_gen2;
    return worklist;
}

/* Adds an item to the worklist, expanding it if needed. */
void MVM_gc_worklist_add_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **item) {
    if (worklist->items == worklist->alloc) {
        worklist->alloc *= 2;
        worklist->list = MVM_realloc(worklist->list, worklist->alloc * sizeof(MVMCollectable **));
    }
    worklist->list[worklist->items++] = item;
}

/* Pre-sizes the worklist in expectation a certain number of items is about to be
 * added. */
void MVM_gc_worklist_presize_for(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMint32 items) {
    if (worklist->items + items >= worklist->alloc) {
        worklist->alloc = worklist->items + items;
        worklist->list = MVM_realloc(worklist->list, worklist->alloc * sizeof(MVMCollectable **));
    }
}

/* Free a worklist. */
void MVM_gc_worklist_destroy(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVM_free(worklist->list);
    MVM_free(worklist);
}
