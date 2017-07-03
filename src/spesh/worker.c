#include "moar.h"

/* The specialization worker thread receives logs from other threads about
 * calls and types that showed up at runtime. It uses this to produce
 * specialized versions of code. */

/* Enters the work loop. */
static void worker(MVMThreadContext *tc, MVMCallsite *callsite, MVMRegister *args) {
    MVMObject *updated_static_frames = MVM_repr_alloc_init(tc,
        tc->instance->boot_types.BOOTArray);
    MVMROOT(tc, updated_static_frames, {
        while (1) {
            MVMObject *log_obj = MVM_repr_shift_o(tc, tc->instance->spesh_queue);
            if (log_obj->st->REPR->ID == MVM_REPR_ID_MVMSpeshLog) {
                MVMSpeshLog *sl = (MVMSpeshLog *)log_obj;
                MVMROOT(tc, sl, {
                    /* Update stats, and if we're logging dump each of them. */
                    tc->instance->spesh_stats_version++;
                    MVM_spesh_stats_update(tc, sl, updated_static_frames);
                    if (tc->instance->spesh_log_fh) {
                        MVMint64 i;
                        MVMint64 n = MVM_repr_elems(tc, updated_static_frames);
                        fprintf(tc->instance->spesh_log_fh,
                            "Statistics Updated\n"
                            "==================\n"
                            "%d frames had their statistics updated.\n\n",
                            (int)n);
                        for (i = 0; i < n; i++) {
                            char *dump = MVM_spesh_dump_stats(tc, (MVMStaticFrame* )
                                MVM_repr_at_pos_o(tc, updated_static_frames, i));
                            fprintf(tc->instance->spesh_log_fh, "%s==========\n\n", dump);
                            MVM_free(dump);
                        }
                    }

                    /* Clear updated static frames array. */
                    MVM_repr_pos_set_elems(tc, updated_static_frames, 0);

                    /* If needed, signal sending thread that it can continue. */
                    if (sl->body.block_mutex) {
                        uv_mutex_lock(sl->body.block_mutex);
                        MVM_store(&(sl->body.completed), 1);
                        uv_cond_signal(sl->body.block_condvar);
                        uv_mutex_unlock(sl->body.block_mutex);
                    }
                });
            }
            else {
                MVM_panic(1, "Unexpected object sent to specialization worker");
            }
        }
    });
}

void MVM_spesh_worker_setup(MVMThreadContext *tc) {
    if (tc->instance->spesh_enabled) {
        MVMObject *worker_entry_point;
        tc->instance->spesh_queue = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTQueue);
        worker_entry_point = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCCode);
        ((MVMCFunction *)worker_entry_point)->body.func = worker;
        MVM_thread_run(tc, MVM_thread_new(tc, worker_entry_point, 1));
    }
}
