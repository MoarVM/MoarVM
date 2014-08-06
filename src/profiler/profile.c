#include "moar.h"

/* Starts profiling with the specified configuration. */
void MVM_profile_start(MVMThreadContext *tc, MVMObject *config) {
    /* Enable profiling. */
    if (tc->instance->profiling)
        MVM_exception_throw_adhoc(tc, "Profiling is already started");
    tc->instance->profiling = 1;
    tc->instance->instrumentation_level++;
}

/* Ends profiling, builds the result data structure, and returns it. */
MVMObject * MVM_profile_end(MVMThreadContext *tc) {
    /* Disable profiling. */
    if (!tc->instance->profiling)
        MVM_exception_throw_adhoc(tc, "Cannot end profiling if not profiling");
    tc->instance->profiling = 0;
    tc->instance->instrumentation_level++;

    /* XXX Result data structure todo. */
    return tc->instance->VMNull;
}
