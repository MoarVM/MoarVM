#include "moar.h"

/* The specialization worker thread receives logs from other threads about
 * calls and types that showed up at runtime. It uses this to produce
 * specialized versions of code. */

/* Enters the work loop. */
static void worker(MVMThreadContext *tc, MVMArgs arg_info) {
    MVMuint64 work_sequence_number = 0;
    MVMObject *updated_static_frames = MVM_repr_alloc_init(tc,
        tc->instance->boot_types.BOOTArray);
    MVMObject *previous_static_frames;
    MVMROOT(tc, updated_static_frames, {
        previous_static_frames = MVM_repr_alloc_init(tc,
            tc->instance->boot_types.BOOTArray);
    });

#ifdef MVM_HAS_PTHREAD_SETNAME_NP
    pthread_setname_np(pthread_self(), "spesh optimizer");
#endif

    tc->instance->speshworker_thread_id = tc->thread_obj->body.thread_id;

    MVMROOT2(tc, updated_static_frames, previous_static_frames, {
        while (1) {
            MVMObject *log_obj;
            MVMuint64 start_time;
            unsigned int interval_id;
            MVMint64 *overview_data = NULL;

            MVMObject *overview_subscription_packet = NULL;

            MVMObject *spesh_overview_event = NULL;

            start_time = uv_hrtime();
            log_obj = MVM_repr_shift_o(tc, tc->instance->spesh_queue);
            if (MVM_spesh_debug_enabled(tc)) {
                MVM_spesh_debug_printf(tc,
                    "Received Logs\n"
                    "=============\n\n"
                    "Was waiting %dus for logs on the log queue.\n\n",
                    (int)((uv_hrtime() - start_time) / 1000));
            }

            if (tc->instance->subscriptions.subscription_queue) {
                spesh_overview_event = tc->instance->subscriptions.SpeshOverviewEvent;
                if (spesh_overview_event) {
                    MVMuint64 now_time = uv_hrtime();

                    MVMROOT(tc, log_obj, {
                        overview_subscription_packet = MVM_repr_alloc(tc, spesh_overview_event);
                    });
                    MVM_gc_root_temp_push(tc, (MVMCollectable **)&overview_subscription_packet);

                    MVM_repr_pos_set_elems(tc, overview_subscription_packet, 15);

                    overview_data = ((MVMArray *)overview_subscription_packet)->body.slots.i64;

                    overview_data[0] = work_sequence_number;
                    overview_data[1] = now_time / 1000;
                    overview_data[2] = (now_time - tc->instance->subscriptions.vm_startup_hrtime) / 1000;
                    overview_data[3] = (now_time - start_time) / 1000;
                }
            }

            if (tc->instance->main_thread->prof_data)
                MVM_profiler_log_spesh_start(tc);

            interval_id = MVM_telemetry_interval_start(tc, "spesh worker consuming a log");

            uv_mutex_lock(&(tc->instance->mutex_spesh_sync));
            tc->instance->spesh_working = 1;
            uv_mutex_unlock(&(tc->instance->mutex_spesh_sync));

            tc->instance->spesh_stats_version++;
            if (log_obj->st->REPR->ID == MVM_REPR_ID_MVMSpeshLog) {
                MVMSpeshLog *sl = (MVMSpeshLog *)log_obj;
                MVM_telemetry_interval_annotate((uintptr_t)sl->body.thread->body.tc, interval_id, "from this thread");
                if (overview_data) {
                    overview_data[4] = sl->body.thread->body.tc->thread_id;
                }
                MVMROOT(tc, sl, {
                    MVMThreadContext *stc;
                    MVMuint32 i;
                    MVMuint32 n;
                    MVMuint64 newly_seen;
                    MVMuint64 updated;

                    MVMuint64 certain_spesh;
                    MVMuint64 observed_spesh;
                    MVMuint64 osr_spesh;

                    /* Update stats, and if we're logging dump each of them. */
                    tc->instance->spesh_stats_version++;
                    start_time = uv_hrtime();
                    MVM_spesh_stats_update(tc, sl, updated_static_frames, &newly_seen, &updated);
                    n = MVM_repr_elems(tc, updated_static_frames);
                    if (MVM_spesh_debug_enabled(tc)) {
                        MVM_spesh_debug_printf(tc,
                            "Statistics Updated\n"
                            "==================\n"
                            "%d frames had their statistics updated in %dus.\n\n",
                            (int)n, (int)((uv_hrtime() - start_time) / 1000));
                        for (i = 0; i < n; i++) {
                            char *dump = MVM_spesh_dump_stats(tc, (MVMStaticFrame* )
                                MVM_repr_at_pos_o(tc, updated_static_frames, i));
                            MVM_spesh_debug_printf(tc, "%s==========\n\n", dump);
                            MVM_free(dump);
                        }
                    }
                    if (overview_data) {
                        overview_data[5] = (uv_hrtime() - start_time) / 1000;
                        overview_data[6] = newly_seen;
                        overview_data[7] = updated;
                    }
                    MVM_telemetry_interval_annotate((uintptr_t)n, interval_id, "stats for this many frames");
                    GC_SYNC_POINT(tc);

                    /* Form a specialization plan. */
                    start_time = uv_hrtime();
                    tc->instance->spesh_plan = MVM_spesh_plan(tc, updated_static_frames, &certain_spesh, &observed_spesh, &osr_spesh);
                    if (MVM_spesh_debug_enabled(tc)) {
                        n = tc->instance->spesh_plan->num_planned;
                        MVM_spesh_debug_printf(tc,
                            "Specialization Plan\n"
                            "===================\n"
                            "%u specialization(s) will be produced (planned in %dus).\n\n",
                            n, (int)((uv_hrtime() - start_time) / 1000));
                        for (i = 0; i < n; i++) {
                            char *dump = MVM_spesh_dump_planned(tc,
                                &(tc->instance->spesh_plan->planned[i]));
                            MVM_spesh_debug_printf(tc, "%s==========\n\n", dump);
                            MVM_free(dump);
                        }
                    }

                    if (overview_data) {
                        overview_data[8] = (uv_hrtime() - start_time) / 1000;
                        overview_data[9] = certain_spesh;
                        overview_data[10] = observed_spesh;
                        overview_data[11] = osr_spesh;
                    }

                    MVM_telemetry_interval_annotate((uintptr_t)tc->instance->spesh_plan->num_planned, interval_id,
                            "this many specializations planned");
                    GC_SYNC_POINT(tc);

                    start_time = uv_hrtime();

                    /* Implement the plan and then discard it. */
                    n = tc->instance->spesh_plan->num_planned;
                    for (i = 0; i < n; i++) {
                        MVM_spesh_candidate_add(tc, &(tc->instance->spesh_plan->planned[i]));
                        GC_SYNC_POINT(tc);
                    }
                    MVM_spesh_plan_destroy(tc, tc->instance->spesh_plan);
                    tc->instance->spesh_plan = NULL;

                    if (overview_data) {
                        overview_data[12] = (uv_hrtime() - start_time) / 1000;
                    }

                    /* Clear up stats that didn't get updated for a while,
                     * then add frames updated this time into the previously
                     * updated array. */
                    MVM_spesh_stats_cleanup(tc, previous_static_frames);
                    n = MVM_repr_elems(tc, updated_static_frames);
                    for (i = 0; i < n; i++)
                        MVM_repr_push_o(tc, previous_static_frames,
                            MVM_repr_at_pos_o(tc, updated_static_frames, i));

                    if (overview_data) {
                        overview_data[13] = n;
                    }

                    /* Clear updated static frames array. */
                    MVM_repr_pos_set_elems(tc, updated_static_frames, 0);

                    /* Allow the sending thread to produce more logs again,
                     * putting a new spesh log in place if needed. */
                    stc = sl->body.thread->body.tc;
                    if (stc) {
                        if (!sl->body.was_compunit_bumped) {
                            if (MVM_incr(&(stc->spesh_log_quota)) == 0) {
                                stc->spesh_log = MVM_spesh_log_create(tc, sl->body.thread);
                                MVM_telemetry_timestamp(stc, "logging restored after quota had run out");
                            }
                        }
                        else {
                            MVM_decr(&(stc->num_compunit_extra_logs));
                        }
                    }

                    /* If needed, signal sending thread that it can continue. */
                    if (sl->body.block_mutex) {
                        uv_mutex_lock(sl->body.block_mutex);
                        MVM_store(&(sl->body.completed), 1);
                        uv_cond_signal(sl->body.block_condvar);
                        uv_mutex_unlock(sl->body.block_mutex);
                    }
                    {
                        MVMSpeshLogEntry *entries = sl->body.entries;
                        sl->body.entries = NULL;
                        MVM_free(entries);
                    }
                });

            }
            else if (MVM_is_null(tc, log_obj)) {
                /* This is a stop signal, so quit processing */
                break;
            } else {
                MVM_panic(1, "Unexpected object sent to specialization worker");
            }

            MVM_telemetry_interval_stop(tc, interval_id, "spesh worker finished");

            if (overview_data) {
                MVMObject *queue = tc->instance->subscriptions.subscription_queue;

                overview_data[14] = (uv_hrtime() - start_time) / 1000;

                overview_data = NULL;

                if (queue) {
                    MVM_repr_push_o(tc, queue, overview_subscription_packet);
                }
                MVM_gc_root_temp_pop(tc);
                overview_subscription_packet = NULL;
            }

            if (tc->instance->main_thread->prof_data)
                MVM_profiler_log_spesh_end(tc);

            uv_mutex_lock(&(tc->instance->mutex_spesh_sync));
            tc->instance->spesh_working = 0;
            uv_cond_broadcast(&(tc->instance->cond_spesh_sync));
            uv_mutex_unlock(&(tc->instance->mutex_spesh_sync));

            work_sequence_number++;
        }
    });
}

/* Not thread safe per instance, but normally only used when instance is still
 * single-threaded */
void MVM_spesh_worker_start(MVMThreadContext *tc) {
    MVMObject *worker_entry_point;
    if (tc->instance->spesh_enabled) {

        /* There must not be a running thread now */
        assert(tc->instance->spesh_thread == NULL);

        /* If we restart the worker, do not reinitialize the queue */
        if (!tc->instance->spesh_queue)
            tc->instance->spesh_queue = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTQueue);
        worker_entry_point = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTCCode);
        ((MVMCFunction *)worker_entry_point)->body.func = worker;

        tc->instance->spesh_thread = MVM_thread_new(tc, worker_entry_point, 1);
        MVM_thread_run(tc, tc->instance->spesh_thread);
    }
}

void MVM_spesh_worker_stop(MVMThreadContext *tc) {
    /* Send stop sentinel */
    if (tc->instance->spesh_enabled) {
        MVM_repr_unshift_o(tc, tc->instance->spesh_queue, tc->instance->VMNull);
    }
}

void MVM_spesh_worker_join(MVMThreadContext *tc) {
    /* Join thread */
    if (tc->instance->spesh_enabled) {
        assert(tc->instance->spesh_thread != NULL);
        MVM_thread_join(tc, tc->instance->spesh_thread);
        tc->instance->spesh_thread = NULL;
    }
}
