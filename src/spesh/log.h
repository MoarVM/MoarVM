/* The number of runs we do to log what we see. */
#define MVM_SPESH_LOG_RUNS  8

/* Information about an inserted guard instruction due to logging. */
struct MVMSpeshLogGuard {
    /* Instruction and containing basic block. */
    MVMSpeshIns *ins;
    MVMSpeshBB  *bb;

    /* Have we made use of the gurad? */
    MVMuint32 used;
};

void MVM_spesh_log_add_logging(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 osr);
