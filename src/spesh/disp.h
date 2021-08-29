size_t MVM_spesh_disp_dispatch_op_info_size(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *callsite);
void MVM_spesh_disp_initialize_dispatch_op_info(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *cs, MVMOpInfo *dispatch_info);
MVMCallsite * MVM_spesh_disp_callsite_for_dispatch_op(MVMuint16 opcode, MVMuint8 *args,
        MVMCompUnit *cu);
MVMSpeshIns *MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshPlanned *p, MVMSpeshIns *ins);
