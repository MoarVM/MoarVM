/* Information about a dispatch resumption. This is held in the record of the
 * resumption itself. */
struct MVMDispResumptionData {
    /* The dispatch program that is being resumed. */
    MVMDispProgram *dp;

    /* The initial arguments to the dispatch (that is, the root one, not
     * the resumption). */
    MVMArgs *initial_arg_info;

    /* The particular resumption that we are resuming here. */
    MVMDispProgramResumption *resumption;

    /* The place where the resumption state to read/write lives. */
    MVMObject **state_ptr;
};

/* Dispatch resumption state. This is held in the record of the dispatch that
 * was resumed. The design is such as to be zero allocation when there is only
 * a single level of resumable dispatch; if there are more, we point to a
 * linked list of them. */
struct MVMDispResumptionState {
    /* The dispatcher that this is the state for. */
    MVMDispDefinition *disp;

    /* The state. */
    MVMObject *state;

    /* State of another dispatcher. */
    MVMDispResumptionState *next;
};

MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data);
MVMuint32 MVM_disp_resume_find_caller(MVMThreadContext *tc, MVMDispResumptionData *data);
MVMRegister MVM_disp_resume_get_init_arg(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 arg_idx);
void MVM_disp_resume_mark_resumption_state(MVMThreadContext *tc, MVMDispResumptionState *res_state,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_disp_resume_destroy_resumption_state(MVMThreadContext *tc,
        MVMDispResumptionState *res_state);
