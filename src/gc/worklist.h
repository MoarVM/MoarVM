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

    /* Whether we should include gen2 entries. */
    MVMuint8 include_gen2;

    /* Whether we should always include frames. */
    MVMuint8 always_frames;
};

/* Turn this on to define a worklist addition that panics if it spots
 * something untoward with an object being added to a worklist. */
#define MVM_GC_WORKLIST_DEBUG_ADD 0

/* Some macros for doing stuff fast with worklists, defined to look like
 * functions since perhaps they become them in the future if needed. */
#if MVM_GC_WORKLIST_DEBUG_ADD
#define MVM_gc_worklist_add(tc, worklist, item) \
    do { \
        MVMCollectable **item_to_add = (MVMCollectable **)(item);\
        if (*item_to_add) { \
            if ((*item_to_add)->owner == 0) \
                MVM_panic(1, "Zeroed owner in item added to GC worklist"); \
            if ((*item_to_add)->flags & MVM_CF_STABLE == 0 && !STABLE(*item_to_add)) \
                MVM_panic(1, "NULL STable in time added to GC worklist"); \
        } \
        if (*item_to_add && (worklist->include_gen2 || !((*item_to_add)->flags & MVM_CF_SECOND_GEN))) { \
            if (worklist->items == worklist->alloc) \
                MVM_gc_worklist_add_slow(tc, worklist, item_to_add); \
            else \
                worklist->list[worklist->items++] = item_to_add; \
        } \
    } while (0)
#else
#define MVM_gc_worklist_add(tc, worklist, item) \
    do { \
        MVMCollectable **item_to_add = (MVMCollectable **)(item);\
        if (*item_to_add && (worklist->include_gen2 || !((*item_to_add)->flags & MVM_CF_SECOND_GEN))) { \
            if (worklist->items == worklist->alloc) \
                MVM_gc_worklist_add_slow(tc, worklist, item_to_add); \
            else \
                worklist->list[worklist->items++] = item_to_add; \
        } \
    } while (0)
#endif

#define MVM_gc_worklist_add_frame(tc, worklist, frame) \
    do { \
        if ((frame) && (worklist->always_frames || MVM_load(&(tc)->instance->gc_seq_number) != MVM_load(&(frame)->gc_seq_number))) { \
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
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc, MVMuint8 include_gen2, MVMuint8 always_frames);
MVM_PUBLIC void MVM_gc_worklist_add_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **item);
MVM_PUBLIC void MVM_gc_worklist_add_frame_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);
void MVM_gc_worklist_presize_for(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMint32 items);
void MVM_gc_worklist_destroy(MVMThreadContext *tc, MVMGCWorklist *worklist);
void MVM_gc_worklist_mark_frame_roots(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* The number of pointers we assume the list may need to hold initially;
 * it will be resized as needed. */
#define MVM_GC_WORKLIST_START_SIZE      256
