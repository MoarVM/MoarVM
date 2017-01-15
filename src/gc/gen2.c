#include "moar.h"

/* Creates a new second generation allocator. */
MVMGen2Allocator * MVM_gc_gen2_create(MVMInstance *i) {
    /* Create allocator data structure. */
    MVMGen2Allocator *al = MVM_malloc(sizeof(MVMGen2Allocator));

    /* Create empty size classes array data structure. */
    al->size_classes = (MVMGen2SizeClass *)MVM_calloc(MVM_GEN2_BINS, sizeof(MVMGen2SizeClass));

    /* Set up overflows area. */
    al->alloc_overflows = MVM_GEN2_OVERFLOWS;
    al->num_overflows = 0;
    al->overflows = MVM_malloc(al->alloc_overflows * sizeof(MVMCollectable *));

    return al;
}

/* Sets up a size class bin in the second generation. */
static void setup_bin(MVMGen2Allocator *al, MVMuint32 bin) {
    /* Work out page size we want. */
    MVMuint32 page_size = MVM_GEN2_PAGE_ITEMS * ((bin + 1) << MVM_GEN2_BIN_BITS);

    /* We'll just allocate a single page to start off with. */
    al->size_classes[bin].num_pages = 1;
    al->size_classes[bin].pages     = MVM_malloc(sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[0]  = MVM_malloc(page_size);

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
    al->size_classes[bin].pages = MVM_realloc(al->size_classes[bin].pages,
        sizeof(void *) * al->size_classes[bin].num_pages);
    al->size_classes[bin].pages[cur_page] = MVM_malloc(page_size);

    /* Set up allocation position and limit. */
    al->size_classes[bin].alloc_pos = al->size_classes[bin].pages[cur_page];
    al->size_classes[bin].alloc_limit = al->size_classes[bin].alloc_pos + page_size;

    /* set the cur_page to a proper value */
    al->size_classes[bin].cur_page = cur_page;
}

/* Allocates space using the second generation allocator and returns
 * a pointer to the allocated space. Does not zero the space or set
 * it up in any way. */
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
        result = MVM_malloc(size);

        /* Add to overflows list. */
        if (al->num_overflows == al->alloc_overflows) {
            al->alloc_overflows *= 2;
            al->overflows = MVM_realloc(al->overflows,
                al->alloc_overflows * sizeof(MVMCollectable *));
        }
        al->overflows[al->num_overflows++] = result;
    }

    return result;
}

/* Allocates space using the second generation allocator and returns
 * a pointer to the allocated space. Promises the memory will be
 * zeroed, except that the MVMCollectable gen 2 flag will get set. */
void * MVM_gc_gen2_allocate_zeroed(MVMGen2Allocator *al, MVMuint32 size) {
    void *a = MVM_gc_gen2_allocate(al, size);
    memset(a, 0, size);
    ((MVMCollectable *)a)->flags = MVM_CF_SECOND_GEN;
    return a;
}

/* Frees all memory associated with the second generation. */
void MVM_gc_gen2_destroy(MVMInstance *i, MVMGen2Allocator *al) {
    MVMint32 j, k;

    /* Remove all pages. */
    for (j = 0; j < MVM_GEN2_BINS; j++) {
        for (k = 0; k < al->size_classes[j].num_pages; k++)
            MVM_free(al->size_classes[j].pages[k]);
        MVM_free(al->size_classes[j].pages);
    }

    /* Free any allocated overflows. */
    for (j = 0; j < al->num_overflows; j++)
        if (al->overflows[j])
            MVM_free(al->overflows[j]);

    /* Clean up allocator data structure. */
    MVM_free(al->size_classes);
    al->size_classes = NULL;
    MVM_free(al->overflows);
    al->overflows = NULL;
    MVM_free(al);
}

/* blindly move pages from one gen2 to another */
void MVM_gc_gen2_transfer(MVMThreadContext *src, MVMThreadContext *dest) {
    MVMGen2Allocator *gen2 = src->gen2, *dest_gen2 = dest->gen2;
    MVMuint32 bin, obj_size, page;
    char ***freelist_insert_pos;

    for (bin = 0; bin < MVM_GEN2_BINS; bin++) {
        MVMuint32 orig_dest_num_pages = dest_gen2->size_classes[bin].num_pages;
        char *cur_ptr, *end_ptr;
        /* If we've nothing allocated in this size class, skip it. */
        if (gen2->size_classes[bin].pages == NULL)
            continue;

        if (dest_gen2->size_classes[bin].pages == NULL)
            dest_gen2->size_classes[bin].free_list = NULL;

        /* Calculate object size for this bin. */
        obj_size = (bin + 1) << MVM_GEN2_BIN_BITS;

        /* freelist_insert_pos is a pointer to a memory location that
         * stores the address of the last traversed free list node (char **). */
        /* Initialize freelist insertion position to free list head. */
        freelist_insert_pos = &gen2->size_classes[bin].free_list;

        if (dest_gen2->size_classes[bin].pages == NULL) {
            dest_gen2->size_classes[bin].pages
                = MVM_malloc(sizeof(void *) * gen2->size_classes[bin].num_pages);
            dest_gen2->size_classes[bin].num_pages = gen2->size_classes[bin].num_pages;
        }
        else {
            dest_gen2->size_classes[bin].num_pages
                += gen2->size_classes[bin].num_pages;
            dest_gen2->size_classes[bin].pages
                = MVM_realloc(dest_gen2->size_classes[bin].pages,
                    sizeof(void *) * dest_gen2->size_classes[bin].num_pages);
        }

        /* Visit each page in the source. */
        for (page = 0; page < gen2->size_classes[bin].num_pages; page++) {
            /* Visit all the objects, looking for dead ones and swap the
             * owner for each of them. */
            cur_ptr = gen2->size_classes[bin].pages[page];
            end_ptr = page + 1 == gen2->size_classes[bin].num_pages
                ? gen2->size_classes[bin].alloc_pos
                : cur_ptr + obj_size * MVM_GEN2_PAGE_ITEMS;
            while (cur_ptr < end_ptr) {
                if (cur_ptr == (char *)freelist_insert_pos) {
                    /* skip */
                }
                else if (cur_ptr == (char *)*freelist_insert_pos) {
/*                    printf("found a free list slot in bin %d page %d: %d with value %d and start %d and limit %d\n",
                        bin, page, cur_ptr, *(void **)cur_ptr, gen2->size_classes[bin].pages[page],
                        dest_gen2->size_classes[bin].alloc_limit);*/
                    freelist_insert_pos = (char ***)cur_ptr;
                }
                else { /* note: we don't have tests that exercise this path yet. */
/*                    printf("updating an owner from %d to %d\n", ((MVMCollectable *)cur_ptr)->owner, dest->thread_id);*/
                    ((MVMCollectable *)cur_ptr)->owner = dest->thread_id;
                }

                /* Move to the next object. */
                cur_ptr += obj_size;
            }
            dest_gen2->size_classes[bin].pages[page + orig_dest_num_pages] = gen2->size_classes[bin].pages[page];
        }

        freelist_insert_pos = &dest_gen2->size_classes[bin].free_list;
        while (*freelist_insert_pos) {
            freelist_insert_pos = (char ***)*freelist_insert_pos;
        }
        /* chain the destination's freelist through any remaining unallocated area */
        if (dest_gen2->size_classes[bin].alloc_pos) {
            cur_ptr = dest_gen2->size_classes[bin].alloc_pos;
            end_ptr = dest_gen2->size_classes[bin].alloc_limit;
            while (cur_ptr < end_ptr) {
                *freelist_insert_pos = (char **)cur_ptr;
                freelist_insert_pos = (char ***)cur_ptr;
                cur_ptr += obj_size;
            }
        }

        /* link to the new pages, if any */
        *freelist_insert_pos = gen2->size_classes[bin].free_list;

        dest_gen2->size_classes[bin].alloc_pos = gen2->size_classes[bin].alloc_pos;
        dest_gen2->size_classes[bin].alloc_limit = gen2->size_classes[bin].alloc_limit;

        MVM_free(gen2->size_classes[bin].pages);
        gen2->size_classes[bin].pages = NULL;
        gen2->size_classes[bin].num_pages = 0;
    }
    { /* copy the roots... */
        MVMuint32 i, n = src->num_gen2roots;
        for ( i = 0; i < n; i++) {
            MVM_gc_root_gen2_add(dest, src->gen2roots[i]);
        }
        src->num_gen2roots = 0;
        src->alloc_gen2roots = 0;
        MVM_free(src->gen2roots);
        src->gen2roots = NULL;
    }
}


void MVM_gc_gen2_compact_overflows(MVMGen2Allocator *al) {
    /* compact the overflow list to prevent it from growing without bounds */
    MVMCollectable **overflows     = al->overflows;
    const MVMuint32  num_overflows = al->num_overflows;
    MVMuint32        cursor = 0;
    MVMuint32        live;

    /* Find the first NULL object. */
    while (cursor < num_overflows && overflows[cursor])
        cursor++;
    live = cursor;

    /* Slide others back so the alive ones are at the start of the list. */
    while (cursor < num_overflows) {
        if (overflows[cursor]) {
            overflows[live++] = overflows[cursor];
        }
        cursor++;
    }

    al->num_overflows = live;
}
