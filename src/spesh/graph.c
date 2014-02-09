/* Allocate a piece of memory from the spesh graph's buffer. Deallocated when
 * the spesh graph is. */
char * spesh_alloc(MVMThreadContext *tc, MVMSpeshGraph *g, size_t bytes) {
    char *result = NULL;
    if (g->mem_block) {
        MVMSpeshMemBlock *block = g->mem_block;
        if (block->alloc + bytes < block->limit) {
            result = block->alloc;
            block->alloc += bytes;
        }
    }
    if (!result) {
        /* No block, or block was full. Add another. */
        MVMSpeshMemBlock *block = malloc(sizeof(MVMSpeshMemBlock));
        block->buffer = calloc(MVM_SPESH_MEMBLOCK_SIZE, 1);
        block->alloc  = block->buffer;
        block->limit  = block->buffer + MVM_SPESH_MEMBLOCK_SIZE;
        block->prev   = g->mem_block;
        g->mem_block  = block;

        /* Now allocate out of it. */
        if (bytes > MVM_SPESH_MEMBLOCK_SIZE)
            MVM_exception_throw_adhoc(tc, "spesh_alloc: requested oversized block");
        result = block->alloc;
        block->alloc += bytes;
    }
    return result;
}

/* Builds the control flow graph, populating the passed spesh graph structure
 * with it. This also makes nodes for all of the instruction. */
static void build_cfg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMStaticFrame *sf) {
    
}

/* Transforms a spesh graph into SSA form. After this, the graph will have all
 * register accesses given an SSA "version", and phi instructions inserted as
 * needed. */
static void ssa(MVMThreadContext *tc, MVMSpeshGraph *g) {
}

/* Takes a static frame and creates a spesh graph for it. */
MVMSpeshGraph * MVM_spesh_graph_create(MVMThreadContext *tc, MVMStaticFrame *sf) {
    /* Create top-level graph object. */
    MVMSpeshGraph *g = calloc(1, sizeof(MVMSpeshGraph));

    /* Build the CFG out of the static frame, and transform it to SSA. */
    build_cfg(tc, g, sf);
    ssa(tc, g);

    /* Hand back the completed graph. */
    return g;
}

/* Destroys a spesh graph, deallocating all its associated memory. */
void MVM_spesh_graph_destroy(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Free all of the allocated node memory. */
    MVMSpeshMemBlock *cur_block = g->mem_blocks;
    while (cur_block) {
        MVMSpeshMemBlock *prev = cur_block->prev;
        free(cur_block->buffer);
        free(cur_block);
        cur_block = prev;
    }

    /* Free the graph itself. */
    free(g);
}
