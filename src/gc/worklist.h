/* A worklist holds the current set of pointers we have got in the queue
 * to scan. We hide away the details of it behind this abstraction since
 * the order in which we hand things back from it can have a big influence
 * on locality in the copied/compacted heap, and thus mutator performance.
 *
 * For the time being, we do the simplest possible thing: just make it a
 * stack. This actually has reasonable properties if we scan objects right
 * after they are copied/moved, since an object's children will then come
 * right after the object itself - unless they were copied/moved earlier.
 * But things aren't quite so rosy for deeply nested data structures, where
 * two siblings may thus end up far apart. Lots of stuff in the literature
 * on these issues, but for now this is probably less bad than some of the
 * other options.
 */
struct MVMGCWorklist {
    /* The worklist itself. An array of addresses which hold pointers to
     * collectables (yes, two levels of indirection, since we need to
     * update addresses in copying/moving algorithms.) */
    MVMCollectable ***list;
    MVMFrame        **frames_list;

    /* The number of items on the worklist. */
    MVMuint32 items;
    MVMuint32 frames;

    /* The number of items the work list is allocated to hold. */
    MVMuint32 alloc;
    MVMuint32 frames_alloc;
};

/* Some macros for doing stuff fast with worklists, defined to look like
 * functions since perhaps they become them in the future if needed. */
#define MVM_gc_worklist_add(tc, worklist, item) \
    do { \
        if (worklist->items == worklist->alloc) \
            MVM_gc_worklist_add_slow(tc, worklist, (MVMCollectable **)(item)); \
        else \
            worklist->list[worklist->items++] = (MVMCollectable **)(item); \
    } while (0)

#define MVM_gc_worklist_add_frame(tc, worklist, frame) \
    do { \
        if ((frame) && (tc)->instance->gc_seq_number != (frame)->gc_seq_number) { \
            if (worklist->frames == worklist->frames_alloc) \
                MVM_gc_worklist_add_frame_slow(tc, worklist, (frame)); \
            else \
                worklist->frames_list[worklist->frames++] = (frame); \
        } \
    } while (0)

#define MVM_gc_worklist_get(tc, worklist) \
    (worklist->items ? \
        worklist->list[--worklist->items] : \
        NULL)

#define MVM_gc_worklist_get_frame(tc, worklist) \
    (worklist->frames ? \
        worklist->frames_list[--worklist->frames] : \
        NULL)

/* Various functions for worklist manipulation. */
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc);
void MVM_gc_worklist_add_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **item);
void MVM_gc_worklist_add_frame_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);
void MVM_gc_worklist_presize_for(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMint32 items);
void MVM_gc_worklist_destroy(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* The number of pointers we assume the list may need to hold initially;
 * it will be resized as needed. */
#define MVM_GC_WORKLIST_START_SIZE      256
