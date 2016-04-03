#include "moar.h"

/* Allocates a new GC worklist. */
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc, MVMuint8 include_gen2, MVMuint8 always_frames) {
    MVMGCWorklist *worklist = MVM_malloc(sizeof(MVMGCWorklist));
    worklist->items = 0;
    worklist->frames = 0;
    worklist->alloc = MVM_GC_WORKLIST_START_SIZE;
    worklist->frames_alloc = MVM_GC_WORKLIST_START_SIZE;
    worklist->list  = MVM_malloc(worklist->alloc * sizeof(MVMCollectable **));
    worklist->frames_list  = MVM_malloc(worklist->frames_alloc * sizeof(MVMFrame *));
    worklist->include_gen2 = include_gen2;
    worklist->always_frames = always_frames;
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

/* Adds an item to the worklist, expanding it if needed. */
void MVM_gc_worklist_add_frame_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    if (worklist->frames == worklist->frames_alloc) {
        worklist->frames_alloc *= 2;
        worklist->frames_list = MVM_realloc(worklist->frames_list, worklist->frames_alloc * sizeof(MVMFrame *));
    }
    worklist->frames_list[worklist->frames++] = frame;
}

/* Adds a lot of items to the worklist that are laid out consecutively in memory, expanding it if needed. */
void MVM_gc_worklist_add_vector(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **firstitem, MVMuint32 count, MVMuint32 offset) {
    MVMint32 index = 0;
    while (worklist->items + count > worklist->alloc) {
        index = 1;
        worklist->alloc *= 2;
    }
    if (index == 1) {
        worklist->list = MVM_realloc(worklist->list, worklist->alloc * sizeof(MVMCollectable **));
    }

    for (index = 0; index < count; index++) {
        worklist->list[worklist->items++] = item;
        firstitem = (MVMCollectable **)((char *)firstitem + offset);
    }
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
    worklist->list = NULL;
    MVM_free(worklist->frames_list);
    worklist->frames_list = NULL;
    MVM_free(worklist);
}

/* Move things from the frames worklist to the object worklist. */
void MVM_gc_worklist_mark_frame_roots(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMFrame *cur_frame;
    while ((cur_frame = MVM_gc_worklist_get_frame((tc), (worklist))))
        MVM_gc_root_add_frame_roots_to_worklist((tc), (worklist), cur_frame);
}
