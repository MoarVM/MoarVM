/* Represents the objects for a particular size class. */
struct MVMGen2SizeClass {
    /* Each page holds a certain number of collectables. We know
     * nothing of the size statically, so we'll work in bytes. */
    char **pages;

    /* Head of the free list. */
    char **free_list;

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

/* An "instance" of the fixed size allocator. */
struct MVMGen2Allocator {
    /* Size classes for the fixed size allocator. Each one represents
     * a bunch of objects of the same size. The allocated sizes are
     * rounded and then one of these buckets is used, unless it is
     * past the limit. */
    MVMGen2SizeClass *size_classes;

    /* Array of objects that were malloc'd instead, because they did
     * not fit in a size class due to being too large. */
    MVMCollectable **overflows;

    /* The number of objects in the overflow array. */
    MVMuint32        num_overflows;

    /* The amount of space allocated in the overflow array. */
    MVMuint32        alloc_overflows;
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
#define MVM_GEN2_BIN_BITS   3

/* Mask used to know if we hit a size class exactly or have to round up. */
#define MVM_GEN2_BIN_MASK   ((1 << MVM_GEN2_BIN_BITS) - 1)

/* Number of bins in the FSA. Beyond this, we just degrade to malloc/free. */
#define MVM_GEN2_BINS       40

/* Default overflow list size. */
#define MVM_GEN2_OVERFLOWS  32

/* The number of items that go into each page. */
#define MVM_GEN2_PAGE_ITEMS 256

/* Functions. */
MVMGen2Allocator * MVM_gc_gen2_create(MVMInstance *i);
void * MVM_gc_gen2_allocate(MVMGen2Allocator *al, MVMuint32 size);
void * MVM_gc_gen2_allocate_zeroed(MVMGen2Allocator *al, MVMuint32 size);
void MVM_gc_gen2_destroy(MVMInstance *i, MVMGen2Allocator *allocator);
void MVM_gc_gen2_transfer(MVMThreadContext *src, MVMThreadContext *dest);
void MVM_gc_gen2_compact_overflows(MVMGen2Allocator *allocator);
