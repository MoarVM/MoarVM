/* The global, top-level data structure for the fixed size allocator. */
struct MVMFixedSizeAlloc {
    /* Size classes for the fixed size allocator. Each one represents a bunch
     * of objects of the same size. The allocated sizes are rounded and then
     * one of these buckets is used (more size classes are allocated if a
     * need arises). */
    MVMFixedSizeAllocSizeClass *size_classes;

    /* Spin lock used for reading from the free list, to avoid ABA. */
    AO_t freelist_spin;

    /* Mutex for when we can't do a cheap/simple allocation. */
    uv_mutex_t complex_alloc_mutex;

    /* Head of the "free at next safepoint" list of overflows (that is,
     * items that don't fit in a fixed size allocator bin). */
    MVMFixedSizeAllocSafepointFreeListEntry *free_at_next_safepoint_overflows;
};

/* Free list entry. Must be no bigger than the smallest size class. */
struct MVMFixedSizeAllocFreeListEntry {
    void *next;
};

/* Entry in the "free at next safe point" linked list. */
struct MVMFixedSizeAllocSafepointFreeListEntry {
    void                                    *to_free;
    MVMFixedSizeAllocSafepointFreeListEntry *next;
};

/* Pages of objects of a particular size class. */
struct MVMFixedSizeAllocSizeClass {
    /* Each page holds allocated chunks of memory. */
    char **pages;

    /* Head of the free list. */
    MVMFixedSizeAllocFreeListEntry *free_list;

    /* The current allocation position if we've nothing on the
     * free list. */
    char *alloc_pos;

    /* The current page allocation limit (once we hit this, we need
     * to go to the next page) Also just used when no free list. */
    char *alloc_limit;

    /* The current page number that we're allocating in. */
    MVMuint32 cur_page;

    /* The number of pages allocated. */
    MVMuint32 num_pages;

    /* Head of the "free at next safepoint" list. */
    MVMFixedSizeAllocSafepointFreeListEntry *free_at_next_safepoint_list;
};

/* The per-thread data structure for the fixed size allocator, hung off the
 * thread context. Holds a free list per size bin. Allocations on the thread
 * will preferentially use the thread free list, and threads will free to
 * their own free lists, up to a length limit. On hitting the limit, they
 * will free back to the global allocator. This helps ensure patterns like
 * producer/consumer don't end up with a "leak". */
struct MVMFixedSizeAllocThread {
    MVMFixedSizeAllocThreadSizeClass *size_classes;
};
struct MVMFixedSizeAllocThreadSizeClass {
    /* Head of the free list. */
    MVMFixedSizeAllocFreeListEntry *free_list;

    /* How many items are on this thread's free list. */
    MVMuint32 items;
};

/* The number of bits we discard from the requested size when binning
 * the allocation request into a size class. For example, if this is
 * 3 bits then:
 *      Request for 2 bytes  ==> bin 0  (objects 0 - 8 bytes)
 *      Request for 4 bytes  ==> bin 0  (objects 0 - 8 bytes)
 *      Request for 8 bytes  ==> bin 0  (objects 0 - 8 bytes)
 *      Request for 12 bytes ==> bin 1  (objects 9 - 16 bytes)
 *      Request for 16 bytes ==> bin 1  (objects 9 - 16 bytes)
 */
#define MVM_FSA_BIN_BITS   3

/* Mask used to know if we hit a size class exactly or have to round up. */
#define MVM_FSA_BIN_MASK   ((1 << MVM_FSA_BIN_BITS) - 1)

/* Number of bins in the FSA. Beyond this, we just degrade to malloc/free. */
#define MVM_FSA_BINS       96

/* The number of items that go into each page. */
#define MVM_FSA_PAGE_ITEMS 128

/* The length limit for the per-thread free list. */
#define MVM_FSA_THREAD_FREELIST_LIMIT   1024

/* Functions. */
MVMFixedSizeAlloc * MVM_fixed_size_create(MVMThreadContext *tc);
void MVM_fixed_size_create_thread(MVMThreadContext *tc);
void * MVM_fixed_size_alloc(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
void * MVM_fixed_size_alloc_zeroed(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
void * MVM_fixed_size_realloc(MVMThreadContext *tc, MVMFixedSizeAlloc *al, void * p, size_t old_bytes, size_t new_bytes);
void * MVM_fixed_size_realloc_at_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *al, void * p, size_t old_bytes, size_t new_bytes);
void MVM_fixed_size_destroy(MVMFixedSizeAlloc *al);
void MVM_fixed_size_destroy_thread(MVMThreadContext *tc);
void MVM_fixed_size_free(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes, void *free);
void MVM_fixed_size_free_at_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes, void *free);
void MVM_fixed_size_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *al);
