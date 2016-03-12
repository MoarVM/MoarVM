#include "moar.h"

/* Check if we're currently taking heap snapshots. */
MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc) {
    return tc->instance->heap_snapshots != NULL;
}

/* Start heap profiling. */
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config) {
    tc->instance->heap_snapshots = MVM_calloc(1, sizeof(MVMHeapSnapshotCollection));
}

/* Grows storage if it's full, zeroing the extension. */
static void grow_storage(void **store, MVMuint32 *num, MVMuint32 *alloc, size_t size) {
    if (*num == *alloc) {
        *alloc = *alloc ? 2 * *alloc : 32;
        *store = MVM_realloc(*store, *alloc * size);
        memset(((char *)*store) + *num * size, 0, (*alloc - *num) * size);
    }
}

/* Drives the overall process of recording a snapshot of the heap. */
static void record_snapshot(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapSnapshot *hs) {
    printf("Recording heap snapshot NYI\n");
}

/* Takes a snapshot of the heap, adding it to the current heap snapshot
 * collection. */
void MVM_profile_heap_take_snapshot(MVMThreadContext *tc) {
    if (MVM_profile_heap_profiling(tc)) {
        MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
        grow_storage(&(col->snapshots), &(col->num_snapshots), &(col->alloc_snapshots),
            sizeof(MVMHeapSnapshot));
        record_snapshot(tc, col, &(col->snapshots[col->num_snapshots]));
        col->num_snapshots++;
    }
}

/* Frees all memory associated with the heap snapshot. */
static void destroy_heap_snapshot_collection(MVMThreadContext *tc) {
    MVMHeapSnapshotCollection *col = tc->instance->heap_snapshots;
    MVMuint32 i;

    for (i = 0; i < col->num_snapshots; i++) {
        MVMHeapSnapshot *hs = &(col->snapshots[i]);
        MVM_free(hs->collectables);
        MVM_free(hs->references);
    }
    MVM_free(col->snapshots);

    /* XXX Free other pieces. */

    MVM_free(col);
    tc->instance->heap_snapshots = NULL;
}

/* Finishes heap profiling, getting the data. */
MVMObject * MVM_profile_heap_end(MVMThreadContext *tc) {
    destroy_heap_snapshot_collection(tc);
    return tc->instance->VMNull;
}
