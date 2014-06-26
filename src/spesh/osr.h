/* How many iterations we must do in a loop to trigger OSR. */
#define MVM_OSR_THRESHOLD 100

/* Function called when OSR is triggered. */
void MVM_spesh_osr(MVMThreadContext *tc);
