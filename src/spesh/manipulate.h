MVM_PUBLIC void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins);
MVM_PUBLIC void MVM_spesh_manipulate_cleanup_ins_deps(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins);
MVM_PUBLIC void MVM_spesh_manipulate_insert_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *previous, MVMSpeshIns *to_insert);
MVM_PUBLIC void MVM_spesh_manipulate_insert_goto(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshBB *target);
void MVM_spesh_manipulate_add_successor(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshBB *succ);
void MVM_spesh_manipulate_remove_successor(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshBB *succ);
MVMSpeshOperand MVM_spesh_manipulate_get_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 kind);
void MVM_spesh_manipulate_release_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand temp);

MVMSpeshBB *MVM_spesh_manipulate_split_BB_at(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins);
