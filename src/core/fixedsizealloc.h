/* The top-level data structure for the fixed size allocator. */
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
};

/* Free list entry. Must be no bigger than the smallest size class. */
struct MVMFixedSizeAllocFreeListEntry {
    void *next;
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
#define MVM_FSA_BINS       64 

/* The number of items that go into each page. */
#define MVM_FSA_PAGE_ITEMS 128

/* Functions. */
MVMFixedSizeAlloc * MVM_fixed_size_create(MVMThreadContext *tc);
void * MVM_fixed_size_alloc(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
void * MVM_fixed_size_alloc_zeroed(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
void MVM_fixed_size_free(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes, void *free);
void MVM_fixed_size_free_at_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes, void *free);
