/* How do we find the resume init arguments for the dispatch? */
typedef enum {
    /* Using an untranslated dispatch program; in this case there's either a
     * run or recorded dispatch entry on the callstack that we read. */
    MVMDispResumptionArgUntranslated,

    /* A translated dispatch program, meaning that we have a list of
     * register indices per (non-constant) argument, and along with
     * that a pointer to the register set where the dispatch was
     * initiated. */
    MVMDispResumptionArgTranslated
} MVMDispResumptionArgSource;

/* Information about a dispatch resumption. This is held in the record of the
 * resumption itself. */
struct MVMDispResumptionData {
    /* The dispatch program that is being resumed. */
    MVMDispProgram *dp;

    /* The particular resumption that we are resuming here. */
    MVMDispProgramResumption *resumption;

    /* The place where the resumption state to read/write lives. */
    MVMObject **state_ptr;

    /* How should we access dispatch arguments? (Discriminates the union
     * below.) */
    MVMDispResumptionArgSource arg_source;

    union {
        struct {
            /* The initial arguments to the dispatch that set up the
             * resumption. */
            MVMArgs *initial_arg_info;

            /* The temporaries of the dispatch program that set up the
             * resumption, which we may need to find some resume
             * initialization arguments in the situation where a resumption
             * sets up a further resumable dispatch. */
            MVMRegister *temps;
        } untran;

        struct {
            /* The registers we'll find arguments and temporaries in (the
             * register set of the frame that initiated the dispatch). */
            MVMRegister *work;

            /* Mapping table of resumption init arguments to registers
             * indices. */
            MVMuint16 *map;
        } tran;
    };
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

MVMuint32 MVM_disp_resume_find_topmost(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 exhausted);
MVMuint32 MVM_disp_resume_find_caller(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 exhausted);
MVMRegister MVM_disp_resume_get_init_arg(MVMThreadContext *tc, MVMDispResumptionData *data,
        MVMuint32 arg_idx);
void MVM_disp_resume_mark_resumption_state(MVMThreadContext *tc, MVMDispResumptionState *res_state,
        MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_disp_resume_destroy_resumption_state(MVMThreadContext *tc,
        MVMDispResumptionState *res_state);
