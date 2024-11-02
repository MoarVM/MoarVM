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

    /* The number of items on the worklist. */
    MVMuint32 items;

    /* The number of items the work list is allocated to hold. */
    MVMuint32 alloc;

    /* Whether we should include gen2 entries. */
    MVMuint8 include_gen2 : 1;

    /* Whether we can rely on only addresses between MVM_NURSERY_ARENA_POS and
     * MVM_NURSERY_ARENA_LIMIT to be in a nursery. */
    MVMuint8 nursery_address_hack_active : 1;
};

/* Some macros for doing stuff fast with worklists, defined to look like
 * functions since perhaps they become them in the future if needed. */
#if MVM_GC_DEBUG
#define MVM_gc_worklist_add(tc, worklist, item) \
    do { \
        MVMCollectable **item_to_add = (MVMCollectable **)(item);\
        if (*item_to_add) { \
            if ((*item_to_add)->owner == 0) \
                MVM_panic(1, "Zeroed owner in item added to GC worklist"); \
            if ((*item_to_add)->owner > tc->instance->next_user_thread_id) \
                MVM_panic(1, "Invalid owner in item added to GC worklist"); \
            if ((*item_to_add)->flags2 & MVM_CF_DEBUG_IN_GEN2_FREE_LIST) \
                MVM_panic(1, "Adding item to worklist already freed in gen2\n"); \
            if ((*item_to_add)->flags1 & MVM_CF_FRAME) {\
                if (!((MVMFrame *)(*item_to_add))->static_info) \
                    MVM_panic(1, "Frame with NULL static_info added to worklist"); \
            }\
            else if (((*item_to_add)->flags1 & MVM_CF_STABLE) == 0 && !STABLE(*item_to_add)) \
                MVM_panic(1, "NULL STable in item added to GC worklist"); \
            if ((char *)*item_to_add >= (char *)tc->nursery_alloc && \
                    (char *)*item_to_add < (char *)tc->nursery_alloc_limit) \
                MVM_panic(1, "Adding pointer %p to past fromspace to GC worklist", \
                    *item_to_add); \
        } \
        if (*item_to_add && (worklist->include_gen2 || !((*item_to_add)->flags2 & MVM_CF_SECOND_GEN))) { \
            if (worklist->items == worklist->alloc) \
                MVM_gc_worklist_add_slow(tc, worklist, item_to_add); \
            else \
                worklist->list[worklist->items++] = item_to_add; \
        } \
    } while (0)
#define MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, item) \
    MVM_gc_worklist_add(tc, worklist, item)
#define MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, item) \
    MVM_gc_worklist_add(tc, worklist, item)
#define MVM_gc_worklist_add_object_no_include_gen2_nocheck(tc, worklist, item) \
    MVM_gc_worklist_add(tc, worklist, item)
#else
#if MVM_USE_NURSERY_ARENA
#define MVM_ITEM_IS_SECOND_GEN(worklist, item) \
    (worklist->nursery_address_hack_active \
        ? !((uintptr_t)(*item) >= (uintptr_t)MVM_NURSERY_ARENA_POS && (uintptr_t)(*item) < (uintptr_t)MVM_NURSERY_ARENA_LIMIT) \
        : (*item)->flags2 & MVM_CF_SECOND_GEN)
#else
#define MVM_ITEM_IS_SECOND_GEN(worklist, item) ((*item)->flags2 & MVM_CF_SECOND_GEN)
#endif
#define MVM_gc_worklist_add(tc, worklist, item) \
    do { \
        MVMCollectable **item_to_add = (MVMCollectable **)(item);\
        if (*item_to_add && (worklist->include_gen2 || !MVM_ITEM_IS_SECOND_GEN(worklist, item_to_add))) { \
            if (worklist->items == worklist->alloc) \
                MVM_gc_worklist_add_slow(tc, worklist, item_to_add); \
            else \
                worklist->list[worklist->items++] = item_to_add; \
        } \
    } while (0)
/* Assumes worklist->include_gen2 is True. Also assumes there is enough space
 * in worklist->items.
 * Make sure to call MVM_gc_worklist_presize_for() and that worklist->include_gen2
 * is True before calling this macro. */
#define MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, item) \
do { \
    worklist->list[worklist->items++] = (MVMCollectable**)(item); \
} while (0)
/* Assumes worklist->include_gen2 is False. Also assumes there is enough space
 * in worklist->items.
 * Make sure to call MVM_gc_worklist_presize_for() and that worklist->include_gen2
 * is False before calling this macro. */
#define MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, item) \
do { \
    if (*item && !MVM_ITEM_IS_SECOND_GEN(worklist, (MVMCollectable **)(item)) ) { \
        worklist->list[worklist->items++] = (MVMCollectable**)item; \
    } \
} while (0)
#define MVM_gc_worklist_add_object_no_include_gen2_nocheck(tc, worklist, object) \
do { \
    if (*object && !MVM_ITEM_IS_SECOND_GEN(worklist, (MVMCollectable **)(object))) { \
        worklist->list[worklist->items++] = (MVMCollectable**)(object); \
    } \
} while (0)
#endif

#define MVM_gc_worklist_get(tc, worklist) \
    (worklist->items ? \
        worklist->list[--worklist->items] : \
        NULL)

/* Various functions for worklist manipulation. */
MVMGCWorklist * MVM_gc_worklist_create(MVMThreadContext *tc, MVMuint8 include_gen2);
MVM_PUBLIC void MVM_gc_worklist_add_slow(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable **item);
void MVM_gc_worklist_presize_for(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMint32 items);
void MVM_gc_worklist_destroy(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* The number of pointers we assume the list may need to hold initially;
 * it will be resized as needed. */
#define MVM_GC_WORKLIST_START_SIZE      256
