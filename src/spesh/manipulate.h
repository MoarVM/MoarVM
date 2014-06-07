void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *ins);
void MVM_spesh_manipulate_insert_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *previous, MVMSpeshIns *to_insert);
void MVM_spesh_manipulate_insert_goto(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshBB *target);
void MVM_spesh_manipulate_remove_successor(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshBB *succ);
