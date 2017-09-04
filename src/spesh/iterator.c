#include "moar.h"

void MVM_spesh_iterator_init(MVMThreadContext *tc, MVMSpeshIterator *iterator, MVMSpeshGraph *graph) {
    iterator->graph = graph;
    iterator->bb    = graph->entry;
    if (graph->entry)
        iterator->ins   = graph->entry->first_ins;
}

void MVM_spesh_iterator_copy(MVMThreadContext *tc, MVMSpeshIterator *from, MVMSpeshIterator *to) {
    memcpy(to, from, sizeof(MVMSpeshIterator));
}

MVMSpeshIns * MVM_spesh_iterator_next_ins(MVMThreadContext *tc, MVMSpeshIterator *iterator) {
    if (iterator->ins)
        iterator->ins = iterator->ins->next;
    return iterator->ins;
}

MVMSpeshBB * MVM_spesh_iterator_next_bb(MVMThreadContext *tc, MVMSpeshIterator *iterator) {
    if (iterator->bb)
        iterator->bb  = iterator->bb->linear_next;
    if (iterator->bb)
        iterator->ins = iterator->bb->first_ins;
    return iterator->bb;
}

void MVM_spesh_iterator_skip_phi(MVMThreadContext *tc, MVMSpeshIterator *iterator) {
    while (iterator->ins && iterator->ins->info->opcode == MVM_SSA_PHI) {
        iterator->ins = iterator->ins->next;
    }
}

