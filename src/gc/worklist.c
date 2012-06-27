#include "moarvm.h"

/* Allocates a new GC worklist. */
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc) {
    MVMGCWorklist *worklist = malloc(sizeof(MVMGCWorklist));
    worklist->items = 0;
    worklist->alloc = MVM_GC_WORKLIST_START_SIZE;
    worklist->list  = malloc(worklist->alloc * sizeof(MVMCollectable **));
    return worklist;
}

/* Adds an item to the worklist, expanding it if needed. */
void MVM_gc_worklist_add_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **item) {
    if (worklist->items == worklist->alloc) {
        worklist->alloc *= 2;
        worklist->list = realloc(worklist->list, worklist->alloc * sizeof(MVMCollectable **));
    }
    worklist->list[worklist->items++] = item;
}

/* Free a worklist. */
void MVM_gc_worklist_destroy(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    free(worklist->list);
    worklist->list = NULL;
    free(worklist);
}
