#include "moar.h"

/* Starts profiling with the specified configuration. */
void MVM_profile_start(MVMThreadContext *tc, MVMObject *config) {
    MVM_profile_instrumented_start(tc, config);
}

/* Ends profiling and returns the result data structure. */
MVMObject * MVM_profile_end(MVMThreadContext *tc) {
    return MVM_profile_instrumented_end(tc);
}
