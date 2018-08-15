/* A block of bump-pointer allocated memory. For single-threaded use only. */
struct MVMRegionBlock {
    /* The memory buffer itself. */
    char *buffer;

    /* Current allocation position. */
    char *alloc;

    /* Allocation limit. */
    char *limit;

    /* Previous, now full, memory block. */
    MVMRegionBlock *prev;
};

struct MVMRegionAlloc {
    MVMRegionBlock *block;
};

/* The default allocation chunk size for memory blocks used to store spesh
 * graph nodes. Power of two is best; we start small also. */
#define MVM_REGIONALLOC_FIRST_MEMBLOCK_SIZE 32768
#define MVM_REGIONALLOC_MEMBLOCK_SIZE       8192

void * MVM_region_alloc(MVMThreadContext *tc, MVMRegionAlloc *alloc, size_t s);
void MVM_region_destroy(MVMThreadContext *tc, MVMRegionAlloc *alloc);
void MVM_region_merge(MVMThreadContext *tc,  MVMRegionAlloc *target, MVMRegionAlloc *source);
