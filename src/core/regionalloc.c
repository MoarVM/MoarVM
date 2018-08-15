#include "moar.h"

void * MVM_region_alloc(MVMThreadContext *tc, MVMRegionAlloc *al, size_t bytes) {
    char *result = NULL;

#if !defined(MVM_CAN_UNALIGNED_INT64) || !defined(MVM_CAN_UNALIGNED_NUM64)
    /* Round up size to next multiple of 8, to ensure alignment. */
    bytes = (bytes + 7) & ~7;
#endif

    if (al->block != NULL && (al->block->alloc + bytes) < al->block->limit) {
        result = al->block->alloc;
        al->block->alloc += bytes;
    } else {
        /* No block, or block was full. Add another. */
        MVMRegionBlock *block = MVM_malloc(sizeof(MVMRegionBlock));
        size_t buffer_size = al->block == NULL
            ? MVM_REGIONALLOC_FIRST_MEMBLOCK_SIZE
            : MVM_REGIONALLOC_MEMBLOCK_SIZE;
        if (buffer_size < bytes)
            buffer_size = bytes;
        block->buffer = MVM_calloc(1, buffer_size);
        block->alloc  = block->buffer;
        block->limit  = block->buffer + buffer_size;
        block->prev   = al->block;
        al->block     = block;

        /* Now allocate out of it. */
        result = block->alloc;
        block->alloc += bytes;
    }
    return result;
}

void MVM_region_destroy(MVMThreadContext *tc, MVMRegionAlloc *alloc) {
    MVMRegionBlock *block = alloc->block;
    /* Free all of the allocated memory. */
    while (block) {
        MVMRegionBlock *prev = block->prev;
        MVM_free(block->buffer);
        MVM_free(block);
        block = prev;
    }
    alloc->block = NULL;
}

/* Link source region into target region, so they can be cleaned up as one */
void MVM_region_merge(MVMThreadContext *tc, MVMRegionAlloc *target, MVMRegionAlloc *source) {
    MVMRegionBlock *block = source->block;
    while (block != NULL) {
        MVMRegionBlock *prev = block->prev;
        block->prev = target->block->prev;
        target->block->prev = block;
        block = prev;
    }
    source->block = NULL;
}
