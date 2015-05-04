#include "moar.h"

/* The fixed size allocator provides a thread-safe mechanism for getting and
 * releasing fixed-size chunks of memory. Requests larger blocks from the
 * operating system, and then allocates out of them. Can certainly be further
 * improved. The free list works like a stack, so you get the most recently
 * freed piece of memory of a given size, which should give good cache
 * behavior. */

/* Turn this on to switch to a mode where we debug by size. */
#define FSA_SIZE_DEBUG 0
#if FSA_SIZE_DEBUG
typedef struct {
    MVMuint64 alloc_size;
    void *memory;
} MVMFixedSizeAllocDebug;
#endif

/* Creates the allocator data structure with bins. */
MVMFixedSizeAlloc * MVM_fixed_size_create(MVMThreadContext *tc) {
    int init_stat;
    MVMFixedSizeAlloc *al = MVM_malloc(sizeof(MVMFixedSizeAlloc));
    al->size_classes = MVM_calloc(MVM_FSA_BINS, sizeof(MVMFixedSizeAllocSizeClass));
    if ((init_stat = uv_mutex_init(&(al->complex_alloc_mutex))) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    al->freelist_spin = 0;
    return al;
}

/* Determine the bin. If we hit a bin exactly then it's off-by-one,
 * since the bins list is base-0. Otherwise we've some extra bits,
 * which round us up to the next bin, but that's a no-op. */
static MVMuint32 bin_for(size_t bytes) {
    MVMuint32 bin = (MVMuint32)(bytes >> MVM_FSA_BIN_BITS);
    if ((bytes & MVM_FSA_BIN_MASK) == 0)
        bin--;
    return bin;
}

/* Sets up a size class bin in the second generation. */
static void setup_bin(MVMFixedSizeAlloc *al, MVMuint32 bin) {
    /* Work out page size we want. */
    MVMuint32 page_size = MVM_FSA_PAGE_ITEMS * ((bin + 1) << MVM_FSA_BIN_BITS);

    /* We'll just allocate a single page to start off with. */
    al->size_classes[bin].num_pages = 1;
    al->size_classes[bin].pages     = MVM_malloc(sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[0]  = MVM_malloc(page_size);

    /* Set up allocation position and limit. */
    al->size_classes[bin].alloc_pos = al->size_classes[bin].pages[0];
    al->size_classes[bin].alloc_limit = al->size_classes[bin].alloc_pos + page_size;
}

/* Adds a new page to a size class bin. */
static void add_page(MVMFixedSizeAlloc *al, MVMuint32 bin) {
    /* Work out page size. */
    MVMuint32 page_size = MVM_FSA_PAGE_ITEMS * ((bin + 1) << MVM_FSA_BIN_BITS);

    /* Add the extra page. */
    MVMuint32 cur_page = al->size_classes[bin].num_pages;
    al->size_classes[bin].num_pages++;
    al->size_classes[bin].pages = MVM_realloc(al->size_classes[bin].pages,
        sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[cur_page] = MVM_malloc(page_size);

    /* Set up allocation position and limit. */
    al->size_classes[bin].alloc_pos = al->size_classes[bin].pages[cur_page];
    al->size_classes[bin].alloc_limit = al->size_classes[bin].alloc_pos + page_size;

    /* set the cur_page to a proper value */
    al->size_classes[bin].cur_page = cur_page;
}

/* Allocates a piece of memory of the specified size, using the FSA. */
static void * alloc_slow_path(MVMThreadContext *tc, MVMFixedSizeAlloc *al, MVMuint32 bin) {
    void *result;

    /* Lock, unless single-threaded. */
    MVMint32 lock = MVM_instance_have_user_threads(tc);
    if (lock)
        uv_mutex_lock(&(al->complex_alloc_mutex));

    /* If we've no pages yet, never encountered this bin; set it up. */
    if (al->size_classes[bin].pages == NULL)
        setup_bin(al, bin);

    /* If we're at the page limit, add a new page. */
    if (al->size_classes[bin].alloc_pos == al->size_classes[bin].alloc_limit)
        add_page(al, bin);

    /* Now we can allocate. */
    result = (void *)al->size_classes[bin].alloc_pos;
    al->size_classes[bin].alloc_pos += (bin + 1) << MVM_FSA_BIN_BITS;

    /* Unlock if we locked. */
    if (lock)
        uv_mutex_unlock(&(al->complex_alloc_mutex));

    return result;
}
void * MVM_fixed_size_alloc(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes) {
#if FSA_SIZE_DEBUG
    MVMFixedSizeAllocDebug *dbg = MVM_malloc(bytes + sizeof(MVMuint64));
    dbg->alloc_size = bytes;
    return &(dbg->memory);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Try and take from the free list (fast path). */
        MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
        MVMFixedSizeAllocFreeListEntry *fle;
        if (MVM_instance_have_user_threads(tc)) {
            /* Multi-threaded; take a lock. Note that the lock is needed in
             * addition to the atomic operations: the atomics allow us to add
             * to the free list in a lock-free way, and the lock allows us to
             * avoid the ABA issue we'd have with only the atomics. */
            while (!MVM_trycas(&(al->freelist_spin), 0, 1)) {
                MVMint32 i = 0;
                while (i < 1024)
                    i++;
            }
            do {
                fle = bin_ptr->free_list;
                if (!fle)
                    break;
            } while (!MVM_trycas(&(bin_ptr->free_list), fle, fle->next));
            MVM_barrier();
            al->freelist_spin = 0;
        }
        else {
            /* Single-threaded; just take it. */
            fle = bin_ptr->free_list;
            if (fle)
                bin_ptr->free_list = fle->next;
        }
        if (fle)
            return (void *)fle;

        /* Failed to take from free list; slow path with the lock. */
        return alloc_slow_path(tc, al, bin);
    }
    else {
        return MVM_malloc(bytes);
    }
#endif
}

/* Allocates a piece of memory of the specified size, using the FSA. Promises
 * it will be zeroed. */
void * MVM_fixed_size_alloc_zeroed(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes) {
    void *allocd = MVM_fixed_size_alloc(tc, al, bytes);
    memset(allocd, 0, bytes);
    return allocd;
}

/* Frees a piece of memory of the specified size, using the FSA. */
void MVM_fixed_size_free(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes, void *to_free) {
#if FSA_SIZE_DEBUG
    MVMFixedSizeAllocDebug *dbg = (MVMFixedSizeAllocDebug *)((char *)to_free - 8);
    if (dbg->alloc_size != bytes)
        MVM_panic(1, "Fixed size allocator: wrong size in free");
    MVM_free(dbg);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Came from a bin; put into free list. */
        MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
        MVMFixedSizeAllocFreeListEntry *to_add  = (MVMFixedSizeAllocFreeListEntry *)to_free;
        MVMFixedSizeAllocFreeListEntry *orig;
        if (MVM_instance_have_user_threads(tc)) {
            /* Multi-threaded; race to add it. */
            do {
                orig = bin_ptr->free_list;
                to_add->next = orig;
            } while (!MVM_trycas(&(bin_ptr->free_list), orig, to_add));
        }
        else {
            /* Single-threaded; just add it. */
            to_add->next       = bin_ptr->free_list;
            bin_ptr->free_list = to_add;
        }
    }
    else {
        /* Was malloc'd due to being oversize, so just free it. */
        MVM_free(to_free);
    }
#endif
}

/* Queues a piece of memory of the specified size to be freed at the next
 * global safe point, using the FSA. A global safe point is defined as one in
 * which all threads, since we requested the freeing of the memory, have
 * reached a safe point. */
void MVM_fixed_size_free_at_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes, void *to_free) {
#if FSA_SIZE_DEBUG
    MVMFixedSizeAllocDebug *dbg = (MVMFixedSizeAllocDebug *)((char *)to_free - 8);
    if (dbg->alloc_size != bytes)
        MVM_panic(1, "Fixed size allocator: wrong size in free");
    MVM_free(dbg);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Came from a bin; put into free list. */
        MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
        MVMFixedSizeAllocFreeListEntry *to_add  = (MVMFixedSizeAllocFreeListEntry *)to_free;
        MVMFixedSizeAllocFreeListEntry *orig;
        if (MVM_instance_have_user_threads(tc)) {
            /* Multi-threaded; race to add it to the "free at next safe point"
             * list. */
            MVM_panic(1, "MVM_fixed_size_free_at_safepoint not yet fully implemented");
        }
        else {
            /* Single-threaded, so no global safepoint issues to care for; put
             * it on the free list right away. */
            to_add->next       = bin_ptr->free_list;
            bin_ptr->free_list = to_add;
        }
    }
    else {
        /* Was malloc'd due to being oversize. */
        if (MVM_instance_have_user_threads(tc)) {
            /* Multi-threaded; race to add it to the "free at next safe point"
             * list. */
            MVM_panic(1, "MVM_fixed_size_free_at_safepoint not yet fully implemented");
        }
        else {
            /* Single threaded, so free it immediately. */
            MVM_free(to_free);
        }
    }
#endif
}
