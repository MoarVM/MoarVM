#include "moar.h"

#include "memdebug.h"

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
#ifdef MVM_VALGRIND_SUPPORT
    int bin_no;
#endif
    MVMFixedSizeAlloc *al = MVM_malloc(sizeof(MVMFixedSizeAlloc));
    al->size_classes = MVM_calloc(MVM_FSA_BINS, sizeof(MVMFixedSizeAllocSizeClass));
    if ((init_stat = uv_mutex_init(&(al->complex_alloc_mutex))) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    al->freelist_spin = 0;
    al->free_at_next_safepoint_overflows = NULL;

    /* All other places where we use valgrind macros are very likely
     * thrown out by dead code elimination. Not 100% sure about this,
     * so we ifdef it out. */
#ifdef MVM_VALGRIND_SUPPORT
    for (bin_no = 0; bin_no < MVM_FSA_BINS; bin_no++)
        VALGRIND_CREATE_MEMPOOL(&al->size_classes[bin_no], MVM_FSA_REDZONE_BYTES, 0);
#endif

    return al;
}

/* Creates the per-thread fixed size allocator state. */
void MVM_fixed_size_create_thread(MVMThreadContext *tc) {
    MVMFixedSizeAllocThread *al = MVM_malloc(sizeof(MVMFixedSizeAllocThread));
    al->size_classes = MVM_calloc(MVM_FSA_BINS, sizeof(MVMFixedSizeAllocThreadSizeClass));
    tc->thread_fsa = al;
}

/* Destroys the global fixed size allocator data structure and all of
 * the memory held within it. */
void MVM_fixed_size_destroy(MVMFixedSizeAlloc *al) {
    int bin_no;

    for (bin_no = 0; bin_no < MVM_FSA_BINS; bin_no++) {
        int page_no;
        int num_pages = al->size_classes[bin_no].num_pages;

        VALGRIND_DESTROY_MEMPOOL(&al->size_classes[bin_no]);

        for (page_no = 0; page_no < num_pages; page_no++) {
            MVM_free(al->size_classes[bin_no].pages[page_no]);
        }
        MVM_free(al->size_classes[bin_no].pages);
    }
    uv_mutex_destroy(&(al->complex_alloc_mutex));

    MVM_free(al->size_classes);
    MVM_free(al);
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
    MVMuint32 page_size = MVM_FSA_PAGE_ITEMS * ((bin + 1) << MVM_FSA_BIN_BITS) + MVM_FSA_REDZONE_BYTES * 2 * MVM_FSA_PAGE_ITEMS;

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
    MVMuint32 page_size = MVM_FSA_PAGE_ITEMS * ((bin + 1) << MVM_FSA_BIN_BITS) + MVM_FSA_REDZONE_BYTES * 2 * MVM_FSA_PAGE_ITEMS;

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

    /* Lock. */
    uv_mutex_lock(&(al->complex_alloc_mutex));

    /* If we've no pages yet, never encountered this bin; set it up. */
    if (al->size_classes[bin].pages == NULL)
        setup_bin(al, bin);

    /* If we're at the page limit, add a new page. */
    if (al->size_classes[bin].alloc_pos == al->size_classes[bin].alloc_limit) {
        add_page(al, bin);
    }

    /* Now we can allocate. */
    result = (void *)(al->size_classes[bin].alloc_pos + MVM_FSA_REDZONE_BYTES);
    al->size_classes[bin].alloc_pos += ((bin + 1) << MVM_FSA_BIN_BITS) + 2 * MVM_FSA_REDZONE_BYTES;

    VALGRIND_MEMPOOL_ALLOC(&al->size_classes[bin], result, (bin + 1) << MVM_FSA_BIN_BITS);

    /* Unlock. */
    uv_mutex_unlock(&(al->complex_alloc_mutex));

    return result;
}
static void * alloc_from_global(MVMThreadContext *tc, MVMFixedSizeAlloc *al, MVMuint32 bin) {
    /* Try and take from the global free list (fast path). */
    MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
    MVMFixedSizeAllocFreeListEntry *fle = NULL;
    /* Multi-threaded, so take a lock. Note that the lock is needed in
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
    if (fle) {
        VALGRIND_MEMPOOL_ALLOC(&al->size_classes[bin], ((void *)fle),
                (bin + 1) << MVM_FSA_BIN_BITS);
        return (void *)fle;
    }

    /* Failed to take from free list; slow path with the lock. */
    return alloc_slow_path(tc, al, bin);
}
void * MVM_fixed_size_alloc(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes) {
#if FSA_SIZE_DEBUG
    MVMFixedSizeAllocDebug *dbg = MVM_malloc(bytes + sizeof(MVMuint64));
    dbg->alloc_size = bytes;
    return &(dbg->memory);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Try and take from the per-thread free list. */
        MVMFixedSizeAllocThreadSizeClass *bin_ptr = &(tc->thread_fsa->size_classes[bin]);
        MVMFixedSizeAllocFreeListEntry *fle = bin_ptr->free_list;
        if (fle) {
            bin_ptr->free_list = fle->next;
            bin_ptr->items--;
            return (void *)fle;
        }
        return alloc_from_global(tc, al, bin);
    }
    return MVM_malloc(bytes);
#endif
}

/* Allocates a piece of memory of the specified size, using the FSA. Promises
 * it will be zeroed. */
void * MVM_fixed_size_alloc_zeroed(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes) {
    void *allocd = MVM_fixed_size_alloc(tc, al, bytes);
    memset(allocd, 0, bytes);
    return allocd;
}

/* Reallocs a piece of memory to the specified size, using the FSA. */
void * MVM_fixed_size_realloc(MVMThreadContext *tc, MVMFixedSizeAlloc *al, void * p, size_t old_bytes, size_t new_bytes) {
    MVMuint32 old_bin = bin_for(old_bytes);
    MVMuint32 new_bin = bin_for(new_bytes);
    if (old_bin == new_bin) {
        return p;
    }
    else if (old_bin < MVM_FSA_BINS || new_bin < MVM_FSA_BINS) {
        void *allocd = MVM_fixed_size_alloc(tc, al, new_bytes);
        memcpy(allocd, p, new_bin > old_bin ? old_bytes : new_bytes);
        MVM_fixed_size_free(tc, al, old_bytes, p);
        return allocd;
    }
    else {
        return MVM_realloc(p, new_bytes);
    }
}

/* Reallocs a piece of memory to the specified size, using the FSA. */
void * MVM_fixed_size_realloc_at_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *al, void * p, size_t old_bytes, size_t new_bytes) {
    MVMuint32 old_bin = bin_for(old_bytes);
    MVMuint32 new_bin = bin_for(new_bytes);
    if (old_bin == new_bin) {
        return p;
    }
    else {
        void *allocd = MVM_fixed_size_alloc(tc, al, new_bytes);
        memcpy(allocd, p, new_bin > old_bin ? old_bytes : new_bytes);
        MVM_fixed_size_free_at_safepoint(tc, al, old_bytes, p);
        return allocd;
    }
}

/* Frees a piece of memory of the specified size, using the FSA. */
static void add_to_global_bin_freelist(MVMThreadContext *tc, MVMFixedSizeAlloc *al,
                                       MVMint32 bin, void *to_free) {
    MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
    MVMFixedSizeAllocFreeListEntry *to_add  = (MVMFixedSizeAllocFreeListEntry *)to_free;
    MVMFixedSizeAllocFreeListEntry *orig;

    VALGRIND_MEMPOOL_FREE(bin_ptr, to_add);
    VALGRIND_MAKE_MEM_DEFINED(to_add, sizeof(MVMFixedSizeAllocFreeListEntry));

    /* Multi-threaded; race to add it. */
    do {
        orig = bin_ptr->free_list;
        to_add->next = orig;
    } while (!MVM_trycas(&(bin_ptr->free_list), orig, to_add));
}
static void add_to_bin_freelist(MVMThreadContext *tc, MVMFixedSizeAlloc *al,
                                MVMint32 bin, void *to_free) {
    MVMFixedSizeAllocThreadSizeClass *bin_ptr = &(tc->thread_fsa->size_classes[bin]);
    if (bin_ptr->items < MVM_FSA_THREAD_FREELIST_LIMIT) {
        MVMFixedSizeAllocFreeListEntry *to_add  = (MVMFixedSizeAllocFreeListEntry *)to_free;
        to_add->next = bin_ptr->free_list;
        bin_ptr->free_list = to_add;
        bin_ptr->items++;
    }
    else {
        add_to_global_bin_freelist(tc, al, bin, to_free);
    }
}
void MVM_fixed_size_free(MVMThreadContext *tc, MVMFixedSizeAlloc *al, size_t bytes, void *to_free) {
#if FSA_SIZE_DEBUG
    MVMFixedSizeAllocDebug *dbg = (MVMFixedSizeAllocDebug *)((char *)to_free - 8);
    if (dbg->alloc_size != bytes)
        MVM_panic(1, "Fixed size allocator: wrong size in free");
    MVM_free(dbg);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Add to freelist chained through a bin. */
        add_to_bin_freelist(tc, al, bin, to_free);
    }
    else {
        /* Was malloc'd due to being oversize, so just free it. */
        MVM_free(to_free);
    }
#endif
}

/* Race to add to the "free at next safe point" overflows list. */
static void add_to_overflows_safepoint_free_list(MVMThreadContext *tc, MVMFixedSizeAlloc *al, void *to_free) {
    MVMFixedSizeAllocSafepointFreeListEntry *orig;
    MVMFixedSizeAllocSafepointFreeListEntry *to_add = MVM_fixed_size_alloc(
        tc, al, sizeof(MVMFixedSizeAllocSafepointFreeListEntry));
    to_add->to_free = to_free;
    do {
        orig = al->free_at_next_safepoint_overflows;
        to_add->next = orig;
    } while (!MVM_trycas(&(al->free_at_next_safepoint_overflows), orig, to_add));
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
    add_to_overflows_safepoint_free_list(tc, al, dbg);
#else
    MVMuint32 bin = bin_for(bytes);
    if (bin < MVM_FSA_BINS) {
        /* Came from a bin; race to add it to the "free at next safe point"
         * list. */
        MVMFixedSizeAllocSizeClass     *bin_ptr = &(al->size_classes[bin]);
        MVMFixedSizeAllocSafepointFreeListEntry *orig;
        MVMFixedSizeAllocSafepointFreeListEntry *to_add = MVM_fixed_size_alloc(
            tc, al, sizeof(MVMFixedSizeAllocSafepointFreeListEntry));
        to_add->to_free = to_free;
        do {
            orig = bin_ptr->free_at_next_safepoint_list;
            to_add->next = orig;
        } while (!MVM_trycas(&(bin_ptr->free_at_next_safepoint_list), orig, to_add));
    }
    else {
        /* Was malloc'd due to being oversize. */
        add_to_overflows_safepoint_free_list(tc, al, to_free);
    }
#endif
}

/* Called when we're at a safepoint, to free everything queued up to be freed
 * at the next safepoint. Assumes that it is only called on one thread at a
 * time, while the world is stopped. */
void MVM_fixed_size_safepoint(MVMThreadContext *tc, MVMFixedSizeAlloc *al) {
    /* Go through bins and process any safepoint free lists. */
    MVMFixedSizeAllocSafepointFreeListEntry *cur, *next;
    MVMint32 bin;
    for (bin = 0; bin < MVM_FSA_BINS; bin++) {
        cur = al->size_classes[bin].free_at_next_safepoint_list;
        while (cur) {
            next = cur->next;
            add_to_bin_freelist(tc, al, bin, cur->to_free);
            MVM_fixed_size_free(tc, al, sizeof(MVMFixedSizeAllocSafepointFreeListEntry), cur);
            cur = next;
        }
        al->size_classes[bin].free_at_next_safepoint_list = NULL;
    }

    /* Free overflows. */
    cur = al->free_at_next_safepoint_overflows;
    while (cur) {
        next = cur->next;
        MVM_free(cur->to_free);
        MVM_fixed_size_free(tc, al, sizeof(MVMFixedSizeAllocSafepointFreeListEntry), cur);
        cur = next;
    }
    al->free_at_next_safepoint_overflows = NULL;
}

/* Destroys per-thread fixed size allocator state. All freelists will be
 * contributed back to the global freelists for the bin size. */
void MVM_fixed_size_destroy_thread(MVMThreadContext *tc) {
    MVMFixedSizeAllocThread *al = tc->thread_fsa;
    int bin;
    for (bin = 0; bin < MVM_FSA_BINS; bin++) {
        MVMFixedSizeAllocThreadSizeClass *bin_ptr = &(al->size_classes[bin]);
        MVMFixedSizeAllocFreeListEntry *fle = bin_ptr->free_list;
        while (fle) {
            MVMFixedSizeAllocFreeListEntry *next = fle->next;
            add_to_global_bin_freelist(tc, tc->instance->fsa, bin, (void *)fle);
            fle = next;
        }
    }
    MVM_free(al->size_classes);
    MVM_free(al);
}
