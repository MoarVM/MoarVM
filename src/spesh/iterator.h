struct MVMSpeshIterator {
    MVMSpeshGraph *graph;
    MVMSpeshBB    *bb;
    MVMSpeshIns   *ins;
};

void MVM_spesh_iterator_init(MVMThreadContext *tc, MVMSpeshIterator *iterator, MVMSpeshGraph *graph);
void MVM_spesh_iterator_copy(MVMThreadContext *tc, MVMSpeshIterator *a, MVMSpeshIterator *b);
MVMSpeshIns * MVM_spesh_iterator_next_ins(MVMThreadContext *tc, MVMSpeshIterator *iterator);
MVMSpeshBB  * MVM_spesh_iterator_next_bb(MVMThreadContext *tc, MVMSpeshIterator *iterator);
void MVM_spesh_iterator_skip_phi(MVMThreadContext *tc, MVMSpeshIterator *iterator);
