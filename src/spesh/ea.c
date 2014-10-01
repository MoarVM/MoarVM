#include "moar.h"

/* Adds an allocating instruction to the list of allocations in the graph. */
static void add_allocation_site(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMSpeshAllocation *allocation = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshAllocation));
    allocation->allocating_ins     = ins;
    allocation->escape_state       = MVM_EA_NOESCAPE;

    if (g->num_allocations == g->alloc_allocations) {
        g->alloc_allocations += 16;
        g->allocations = MVM_spesh_alloc(tc, g, g->alloc_allocations * sizeof(MVMSpeshAllocation *)); /* XXX */
    }
    g->allocations[g->num_allocations++] = allocation;
}

/* Performs escape analysis on the spesh graph. */
void MVM_spesh_ea(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb = g->entry;
    while (cur_bb) {
        MVMSpeshIns *cur_ins = cur_bb->first_ins;
        while (cur_ins) {
            /* If it's an allocating instruction, record the allocation site
             * and mark it as not escaping. */
            if (cur_ins->info->allocates)
                add_allocation_site(tc, g, cur_ins);

            cur_ins = cur_ins->next;
        }
        cur_bb = cur_bb->linear_next;
    }
}
