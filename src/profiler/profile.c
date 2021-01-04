#include "moar.h"

/* Starts profiling with the specified configuration. */
void MVM_profile_start(MVMThreadContext *tc, MVMObject *config) {
    if (tc->instance->profiling || MVM_profile_heap_profiling(tc))
        MVM_exception_throw_adhoc(tc, "Profiling is already started");

    if (MVM_repr_exists_key(tc, config, tc->instance->str_consts.kind)) {
        MVMString *kind = MVM_repr_get_str(tc,
            MVM_repr_at_key_o(tc, config, tc->instance->str_consts.kind));
        if (MVM_string_equal(tc, kind, tc->instance->str_consts.instrumented)) {
            MVMuint32 i;
            MVMuint64 s, e;
            MVM_profile_instrumented_start(tc, config);

            /* Call the profiling functions a bunch of times and record how long they took. */
            s = uv_hrtime();
            /* Need an intitial frame for the call_graph as the profiler assumes sensibly that
             * there's only 1 top level frame. Otherwise we'd leak all frames but the very
             * first */
            MVM_profile_log_enter(tc, tc->cur_frame->static_info, MVM_PROFILE_ENTER_NORMAL);
            for (i = 1; i < 1000; i++) {
                MVM_profile_log_enter(tc, tc->cur_frame->static_info, MVM_PROFILE_ENTER_NORMAL);
                MVM_profile_log_exit(tc);
            }
            MVM_profile_log_exit(tc);
            e = uv_hrtime();
            tc->instance->profiling_overhead = (MVMuint64) ((e - s) / 1000) * 0.9;

            /* Disable profiling and discard the data we just collected. */
            uv_mutex_lock(&(tc->instance->mutex_spesh_sync));
            while (tc->instance->spesh_working != 0)
                uv_cond_wait(&(tc->instance->cond_spesh_sync), &(tc->instance->mutex_spesh_sync));
            tc->instance->profiling = 0;
            MVM_free_null(tc->prof_data->collected_data);

            MVM_profile_instrumented_free_data(tc);

            uv_mutex_unlock(&(tc->instance->mutex_spesh_sync));

            /* Now start profiling for real. */
            MVM_profile_instrumented_start(tc, config);
            MVM_profile_log_enter(tc, tc->cur_frame->static_info, MVM_PROFILE_ENTER_NORMAL);
        }
        else if (MVM_string_equal(tc, kind, tc->instance->str_consts.heap))
            MVM_profile_heap_start(tc, config);
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
    if (tc->instance->profiling) {
        MVM_profile_log_exit(tc);
        return MVM_profile_instrumented_end(tc);
    }
    else if (MVM_profile_heap_profiling(tc))
        return MVM_profile_heap_end(tc);
    else
        MVM_exception_throw_adhoc(tc, "Cannot end profiling if not profiling");
}
