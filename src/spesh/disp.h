/* Information held about a dispatch with resume initialization arguments. */
struct MVMSpeshResumeInit {
    /* The dispatch program. */
    MVMDispProgram *dp;

    /* The deopt index of the dispatch. Deoptimization already knows how to
     * locate this, so we can reuse that logic in order to work out the
     * deopt index of the current dispatch. */
    MVMint32 deopt_idx;

    /* The index of the resumption within the dispatch program. */
    MVMuint16 res_idx;

    /* The number of the register reserved to hold resumption init
     * state. Populated at code-gen time. */
    MVMuint16 state_register;

    /* Pointer to a list of registers that hold arguments and temporaries,
     * and thus can be used to construct the resume init args. Also populated
     * at code-gen time. */
    MVMuint16 *init_registers;
};

size_t MVM_spesh_disp_dispatch_op_info_size(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *callsite);
void MVM_spesh_disp_initialize_dispatch_op_info(MVMThreadContext *tc,
        const MVMOpInfo *base_info, MVMCallsite *cs, MVMOpInfo *dispatch_info);
MVMOpInfo * MVM_spesh_disp_create_resumption_op_info(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMDispProgram *dp, MVMuint16 res_idx);
MVMCallsite * MVM_spesh_disp_callsite_for_dispatch_op(MVMuint16 opcode, MVMuint8 *args,
        MVMCompUnit *cu);
MVMSpeshIns *MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshPlanned *p, MVMSpeshIns *ins);
