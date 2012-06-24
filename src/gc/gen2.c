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
