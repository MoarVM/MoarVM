#include "moar.h"

/* Log that we're entering a new frame. */
void MVM_profile_log_enter(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint32 mode) {
}

/* Log that we're exiting a frame normally. */
void MVM_profile_log_exit(MVMThreadContext *tc) {
}

/* Log that we've just allocated the passed object (just log the type). */
void MVM_profile_log_allocated(MVMThreadContext *tc, MVMObject *obj) {
}
