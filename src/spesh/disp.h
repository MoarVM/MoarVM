MVMOpInfo * MVM_spesh_disp_create_dispatch_op_info(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMOpInfo *base_info, MVMCallsite *cs);
void MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p, MVMSpeshIns *ins);
