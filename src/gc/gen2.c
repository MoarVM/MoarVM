#include "moarvm.h"

/* Creates a new second generation allocator. */
MVMGen2Allocator * MVM_gc_gen2_create(MVMInstance *i) {
    /* Create allocator data structure. */
    MVMGen2Allocator *al = malloc(sizeof(MVMGen2Allocator));
    
    /* Create empty size classes array data structure. */
    al->size_classes = malloc(sizeof(MVMGen2SizeClass) * MVM_GEN2_BINS);
    memset(al->size_classes, 0, sizeof(MVMGen2SizeClass) * MVM_GEN2_BINS);
    
    /* Set up overflows area. */
    al->alloc_overflows = MVM_GEN2_OVERFLOWS;
    al->num_overflows = 0;
    al->overflows = malloc(al->alloc_overflows * sizeof(MVMCollectable *));
    
    return al;
}

/* Sets up a size class bin in the second generation. */
static void setup_bin(MVMGen2Allocator *al, MVMuint32 bin) {
    /* Work out page size we want. */
    MVMuint32 page_size = MVM_GEN2_PAGE_ITEMS * ((bin + 1) << MVM_GEN2_BIN_BITS);
    
    /* We'll just allocate a single page to start off with. */
    al->size_classes[bin].num_pages = 1;
    al->size_classes[bin].pages     = malloc(sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[0]  = malloc(page_size);
    
    /* Set up allocation position and limit. */
    al->size_classes[bin].alloc_pos = al->size_classes[bin].pages[0];
    al->size_classes[bin].alloc_limit = al->size_classes[bin].alloc_pos + page_size;
    
    /* Free list is empty until GC run (and we just do page by page allocation). */
    al->size_classes[bin].free_list = NULL;
}

/* Adds a new page to a size class bin. */
static void add_page(MVMGen2Allocator *al, MVMuint32 bin) {
    /* Work out page size. */
    MVMuint32 page_size = MVM_GEN2_PAGE_ITEMS * ((bin + 1) << MVM_GEN2_BIN_BITS);
    
    /* Add the extra page. */
    MVMuint32 cur_page = al->size_classes[bin].num_pages;
    al->size_classes[bin].num_pages++;
    al->size_classes[bin].pages = realloc(al->size_classes[bin].pages,
        sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[cur_page] = malloc(page_size);
    
    /* Set up allocation position and limit. */
    al->size_classes[bin].alloc_pos = al->size_classes[bin].pages[cur_page];
    al->size_classes[bin].alloc_limit = al->size_classes[bin].alloc_pos + page_size;
}

/* Allocates space using the second generation allocator and returns
 * a pointer to the allocated space. */
void * MVM_gc_gen2_allocate(MVMGen2Allocator *al, MVMuint32 size) {
    void *result;
    
    /* Determine the bin. If we hit a bin exactly then it's off-by-one,
     * since the bins list is base-0. Otherwise we've some extra bits,
     * which round us up to the next bin, but that's a no-op. */
    MVMuint32 bin = (size >> MVM_GEN2_BIN_BITS);
    if ((size & MVM_GEN2_BIN_MASK) == 0)
        bin--;

    /* If the selected bin is in range... */
    if (bin < MVM_GEN2_BINS) {
        /* If we've no pages yet, never encountered this bin; set it up. */
        if (al->size_classes[bin].pages == NULL)
            setup_bin(al, bin);
        
        /* If there's a free list entry, use that. */
        if (al->size_classes[bin].free_list) {
            result = (void *)al->size_classes[bin].free_list;
            al->size_classes[bin].free_list = (char **)*(al->size_classes[bin].free_list);
        }
        else {
            /* If we're at the page limit, add a new page. */
            if (al->size_classes[bin].alloc_pos == al->size_classes[bin].alloc_limit)
                add_page(al, bin);
            
            /* Now we can allocate. */
            result = al->size_classes[bin].alloc_pos;
            al->size_classes[bin].alloc_pos += (bin + 1) << MVM_GEN2_BIN_BITS;
        }
    }
    else {
        /* We're beyond the size class bins, so resort to malloc. */
        result = malloc(size);
        
        /* Add to overflows list. */
        if (al->num_overflows == al->alloc_overflows) {
            al->alloc_overflows *= 2;
            al->overflows = realloc(al->overflows,
                al->alloc_overflows * sizeof(MVMCollectable *));
        }
        al->overflows[al->num_overflows++] = result;
    }
    
    return result;
}

/* Frees all memory associated with the second generation. */
void MVM_gc_gen2_destroy(MVMInstance *i, MVMGen2Allocator *al) {
    /* Remove all pages. */
    /* XXX TODO */
    
    /* Free any allocated overflows. */
    /* XXX TODO */
    
    /* Clean up allocator data structure. */
    free(al->size_classes);
    al->size_classes = NULL;
    free(al->overflows);
    al->overflows = NULL;
    free(al);
}
