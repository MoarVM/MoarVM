#include "moar.h"

/* Starts profiling with the specified configuration. */
void MVM_profile_start(MVMThreadContext *tc, MVMObject *config) {
    if (tc->instance->profiling)
        MVM_exception_throw_adhoc(tc, "Profiling is already started");

    if (MVM_repr_exists_key(tc, config, tc->instance->str_consts.kind)) {
        MVMString *kind = MVM_repr_get_str(tc,
            MVM_repr_at_key_o(tc, config, tc->instance->str_consts.kind));
        if (MVM_string_equal(tc, kind, tc->instance->str_consts.instrumented))
            MVM_profile_instrumented_start(tc, config);
        else
            MVM_exception_throw_adhoc(tc, "Unknown profiler specified");
    }
    else {
        /* Default to instrumented if no profiler kind specified, since that
         * used to be the only one we supported. */
        MVM_profile_instrumented_start(tc, config);
    }
}

/* Ends profiling and returns the result data structure. */
MVMObject * MVM_profile_end(MVMThreadContext *tc) {
    if (tc->instance->profiling)
        return MVM_profile_instrumented_end(tc);
    else
        MVM_exception_throw_adhoc(tc, "Cannot end profiling if not profiling");
}
