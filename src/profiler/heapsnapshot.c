#include "moar.h"

/* Temporary state objects kept while making a snapshot. */
typedef struct {
    /* The kind of collectable. */
    MVMuint16 kind;

    /* Index in the collectables (assigned upon adding to the worklist). */
    MVMuint64 col_idx;

    /* Target collectable, if any. */
    void *target;
} WorkItem;
typedef struct {
    /* The heap snapshot collection and current working snapshot. */
    MVMHeapSnapshotCollection *col;
    MVMHeapSnapshot *hs;

    /* Our current collectable worklist. */
    WorkItem *workitems;
    MVMuint64 num_workitems;
    MVMuint64 alloc_workitems;

    /* The collectable we're currently adding references for. */
    MVMuint64 ref_from;
} SnapshotState;

/* Check if we're currently taking heap snapshots. */
MVMint32 MVM_profile_heap_profiling(MVMThreadContext *tc) {
    return tc->instance->heap_snapshots != NULL;
}

/* Start heap profiling. */
void MVM_profile_heap_start(MVMThreadContext *tc, MVMObject *config) {
    tc->instance->heap_snapshots = MVM_calloc(1, sizeof(MVMHeapSnapshotCollection));
}

/* Grows storage if it's full, zeroing the extension. Assumes it's only being
 * grown for one more item. */
static void grow_storage(void **store, MVMuint64 *num, MVMuint64 *alloc, size_t size) {
    if (*num == *alloc) {
        *alloc = *alloc ? 2 * *alloc : 32;
        *store = MVM_realloc(*store, *alloc * size);
        memset(((char *)*store) + *num * size, 0, (*alloc - *num) * size);
    }
}

/* Get a string heap index for the specified C string, adding it if needed. */
 static MVMuint64 get_string_index(MVMThreadContext *tc, SnapshotState *ss,
                                   char *str, char is_const) {
     MVMuint64 i;

     /* Add a lookup hash here if it gets to be a hotspot. */
     MVMHeapSnapshotCollection *col = ss->col;
     for (i = 0; i < col->num_strings; i++)
        if (strcmp(col->strings[i], str) == 0)
            return i;

    grow_storage((void **)&(col->strings), &(col->num_strings),
        &(col->alloc_strings), sizeof(char *));
    grow_storage(&(col->strings_free), &(col->num_strings_free),
        &(col->alloc_strings_free), sizeof(char));
    col->strings[col->num_strings] = str;
    col->strings_free[col->num_strings] = !is_const;
    return col->num_strings++;
 }

/* Push a collectable to the list of work items, allocating space for it and
 * returning the collectable index. */
static MVMuint64 push_workitem(MVMThreadContext *tc, SnapshotState *ss, MVMuint16 kind, void *target) {
    WorkItem *wi;
    MVMuint64 col_idx;

    /* Mark space in collectables collection, and allocate an index. */
    grow_storage(&(ss->hs->collectables), &(ss->hs->num_collectables),
        &(ss->hs->alloc_collectables), sizeof(MVMHeapSnapshotCollectable));
    col_idx = ss->hs->num_collectables;
    ss->hs->num_collectables++;

    /* Add to the worklist. */
    grow_storage(&(ss->workitems), &(ss->num_workitems), &(ss->alloc_workitems),
        sizeof(WorkItem));
    wi = &(ss->workitems[ss->num_workitems]);
    wi->kind = kind;
    wi->col_idx = col_idx;
    wi->target = NULL;
    ss->num_workitems++;

    return col_idx;
}

/* Pop a work item. */
static WorkItem pop_workitem(MVMThreadContext *tc, SnapshotState *ss) {
    ss->num_workitems--;
    return ss->workitems[ss->num_workitems];
}

/* Sets the current reference "from" collectable. */
static void set_ref_from(MVMThreadContext *tc, SnapshotState *ss, MVMuint64 col_idx) {
    /* The references should be contiguous, so if this collectable already
     * has any, something's wrong. */
    if (ss->hs->collectables[col_idx].num_refs)
        MVM_panic(1, "Heap snapshot corruption: can not add non-contiguous refs");

    ss->ref_from = col_idx;
    ss->hs->collectables[col_idx].refs_start = ss->hs->num_references;
}

/* Adds a reference. */
static void add_reference(MVMThreadContext *tc, SnapshotState *ss, MVMuint16 ref_kind,
                          MVMuint64 index, MVMuint64 to) {
    /* Add to the references collection. */
    MVMHeapSnapshotReference *ref;
    MVMuint64 description = (index << MVM_SNAPSHOT_REF_KIND_BITS) | ref_kind;
    grow_storage(&(ss->hs->references), &(ss->hs->num_references),
        &(ss->hs->alloc_references), sizeof(MVMHeapSnapshotReference));
    ref = &(ss->hs->references[ss->hs->num_references]);
    ref->description = description;
    ref->collectable_index = to;
    ss->hs->num_references++;

    /* Increment collectable's number of references. */
    ss->hs->collectables[ss->ref_from].num_refs++;
}

/* Adds a reference with an integer description. */
// XXX

/* Adds a reference with a C string description. */
static void add_reference_cstr(MVMThreadContext *tc, SnapshotState *ss,
                               char *cstr,  MVMuint64 to) {
    MVMuint64 str_idx = get_string_index(tc, ss, cstr, 0);
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Adds a reference with a constant C string description. */
static void add_reference_const_cstr(MVMThreadContext *tc, SnapshotState *ss,
                                     const char *cstr,  MVMuint64 to) {
    MVMuint64 str_idx = get_string_index(tc, ss, (char *)cstr, 1);
    add_reference(tc, ss, MVM_SNAPSHOT_REF_KIND_STRING, str_idx, to);
}

/* Adds a references with a string description. */
// XXX

/* Processes the work items, until we've none left. */
static void process_workitems(MVMThreadContext *tc, SnapshotState *ss) {
    while (ss->num_workitems > 0) {
        WorkItem item = pop_workitem(tc, ss);
        MVMHeapSnapshotCollectable *col = &(ss->hs->collectables[item.col_idx]);

        col->kind = item.kind;
        set_ref_from(tc, ss, item.col_idx);

        switch (item.kind) {
            case MVM_SNAPSHOT_COL_KIND_PERM_ROOTS:
                /* XXX MVM_gc_root_add_permanents_to_worklist(tc, worklist); */
                break;
            case MVM_SNAPSHOT_COL_KIND_INSTANCE_ROOTS:
                /* XXX MVM_gc_root_add_instance_roots_to_worklist(tc, worklist); */
                break;
            case MVM_SNAPSHOT_COL_KIND_CSTACK_ROOTS:
                /* XXX MVM_gc_root_add_temps_to_worklist(tc, worklist); */
                break;
            case MVM_SNAPSHOT_COL_KIND_THREAD_ROOTS:
                /* XXX 
                 * MVM_gc_root_add_tc_roots_to_worklist(tc, worklist);
                 * MVM_gc_worklist_add_frame(tc, worklist, tc->cur_frame);
                 */
                 break;
            case MVM_SNAPSHOT_COL_KIND_ROOT:
                add_reference_const_cstr(tc, ss, "Permanent Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_PERM_ROOTS, NULL));
                add_reference_const_cstr(tc, ss, "VM Instance Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_INSTANCE_ROOTS, NULL));
                add_reference_const_cstr(tc, ss, "C Stack Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_CSTACK_ROOTS, NULL));
                add_reference_const_cstr(tc, ss, "Thread Roots",
                    push_workitem(tc, ss, MVM_SNAPSHOT_COL_KIND_THREAD_ROOTS, NULL));
                 break;
            default:
                MVM_panic(1, "Unknown heap snapshot worklist item kind %d", item.kind);
        }
    }
}

/* Drives the overall process of recording a snapshot of the heap. */
static void record_snapshot(MVMThreadContext *tc, MVMHeapSnapshotCollection *col, MVMHeapSnapshot *hs) {
    MVMuint64 perm_root_synth;

    /* Iinitialize state for taking a snapshot. */
    SnapshotState ss;
    memset(&ss, 0, sizeof(SnapshotState));
    ss.col = col;
    ss.hs = hs;

    /* We push the ultimate "root of roots" onto the worklist to get things
     * going, then set off on our merry way. */
    printf("Recording heap snapshot\n");
    push_workitem(tc, &ss, MVM_SNAPSHOT_COL_KIND_ROOT, NULL);
    process_workitems(tc, &ss);
    printf("Recording completed\n");

    /* Clean up temporary state. */
    MVM_free(ss.workitems);
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
