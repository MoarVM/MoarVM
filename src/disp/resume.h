/* Information about a dispatch resumption. */
struct MVMDispResumptionData {
    /* The dispatch program that is being resumed. */
    MVMDispProgram *dp;

    /* The initial arguments to the dispatch (that is, the root one, not
     * the resumption). */
    MVMArgs *initial_arg_info;

    /* The particular resumption that we are resuming here. */
    MVMDispProgramResumption *resumption;
};

MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data);
MVMRegister MVM_disp_resume_get_init_arg(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 arg_idx);
