/* How many iterations we must do in a loop to trigger OSR. */
#define MVM_OSR_THRESHOLD 200

/* Functions called when OSR is triggered, and after logging runs are done. */
void MVM_spesh_osr(MVMThreadContext *tc);
void MVM_spesh_osr_finalize(MVMThreadContext *tc);
