#include "moarvm.h"

#define GCORCH_DEGUG 1
#define GCORCH_LOG(tc, msg) if (GCORCH_DEGUG) printf("Thread %d : %s", tc->thread_id, msg)

/* If we steal the job of doing GC for a thread, we add it to our stolen
 * list. Has one extra slot at the end for null sentinel. */
static void add_stolen(MVMThreadContext *us, MVMThreadContext *stolen) {
    MVMuint32 i = 0;
    if (us->stolen_for_gc == NULL) {
        us->stolen_for_gc = malloc(us->instance->gc_orch->expected_gc_threads * sizeof(MVMThreadContext *));
        memset(us->stolen_for_gc, 0, us->instance->gc_orch->expected_gc_threads * sizeof(MVMThreadContext *));
    }
    while (us->stolen_for_gc[i])
        i++;
    us->stolen_for_gc[i] = stolen;
}

/* Goes through all threads but the current one and notifies them that a
 * GC run is starting. Those that are blocked are considered excluded from
 * the run, but still counted. */
static void signal_one_thread(MVMThreadContext *us, MVMThreadContext *to_signal) {
    /* Loop here since we may not succeed first time (e.g. the status of the
     * thread may change between the two ways we try to twiddle it). */
    while (1) {
        /* Try to set it from running to interrupted - the common case. */
        if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_INTERRUPT,
                MVMGCStatus_NONE) == MVMGCStatus_NONE)
            return;
        
        /* Otherwise, it's blocked; try to set it to work stolen. */
        if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_STOLEN,
                MVMGCStatus_UNABLE) == MVMGCStatus_UNABLE
           || apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_REAPING,
                MVMGCStatus_DYING) == MVMGCStatus_DYING) {
            /* We stole the work; it's now sufficiently opted in to GC that
             * we can increment the count of threads that are opted in. */
            add_stolen(us, to_signal);
            apr_atomic_inc32(&to_signal->instance->gc_orch->start_votes);
            GCORCH_LOG(us, "A blocked thread spotted; work stolen\n");
            return;
        }
    }    
}
static void signal_all_but(MVMThreadContext *tc) {
    MVMInstance *ins = tc->instance;
    MVMuint32 i;
    if (ins->main_thread != tc)
        signal_one_thread(tc, ins->main_thread);
    for (i = 0; i < ins->num_user_threads; i++) {
        MVMThreadContext *target = ins->user_threads[i]->body.tc;
        if (target != tc)
            signal_one_thread(tc, target);
    }
}

/* Waits for all threads to have enlisted in the GC run. For now, just stupid
 * spinning. */
static void wait_for_all_threads(MVMThreadContext *tc, MVMInstance *i) {
    GCORCH_LOG(tc, "Waiting for all threads...\n");
    while (i->gc_orch->start_votes != i->gc_orch->expected_gc_threads)
        1;
    GCORCH_LOG(tc, "All threads now registered for the GC run\n");
}

/* Does work in a thread's in-tray, if any. */
static void process_in_tray(MVMThreadContext *tc, MVMuint8 gen) {
    /* Do we have any more work given by another thread? If so, re-enter
     * GC loop to process it. Note that since we're now doing GC stuff
     * again, we take back our vote to finish. */
    if (tc->gc_in_tray) {
        GCORCH_LOG(tc, "Was given extra work by another thread; doing it\n");
        apr_atomic_dec32(&tc->instance->gc_orch->finish_votes);
        MVM_gc_collect(tc, MVMGCWhatToDo_InTray, gen);
        apr_atomic_inc32(&tc->instance->gc_orch->finish_votes);
    }
}

/* Called by a thread when it thinks it is done with GC. It may get some more
 * work yet, though. Called by all threads except the co-ordinator. */
static void finish_gc(MVMThreadContext *tc, MVMuint8 gen) {
    /* Vote to finish. */
    GCORCH_LOG(tc, "Waiting for GC termination...\n");
    apr_atomic_inc32(&tc->instance->gc_orch->finish_votes);
    
    /* Loop until other threads have terminated, processing any extra work
     * that we are given. The coordinator decrements its count last, which
     * is how we know all is over. */
    while (tc->instance->gc_orch->finish_votes != tc->instance->gc_orch->expected_gc_threads) {
        process_in_tray(tc, gen);
    }
    
    /* Now we agree we're done, decrement threads still to acknowledge. */
    apr_atomic_dec32(&tc->instance->gc_orch->finish_ack_remaining);
}

/* Cleans up after a GC run, resetting flags and so forth. */
static void cleanup_all(MVMThreadContext *tc) {
    /* Reset GC status flags for any stolen threads. */
    if (tc->stolen_for_gc) {
        MVMuint32 i = 0;
        while (tc->stolen_for_gc[i]) {
            apr_atomic_cas32(&tc->stolen_for_gc[i]->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_STOLEN);
            apr_atomic_cas32(&tc->stolen_for_gc[i]->gc_status, MVMGCStatus_REAPED,
                MVMGCStatus_REAPING);
            i++;
        }
    }
    
    /* Reset status for all other threads. */
    apr_atomic_cas32(&tc->instance->main_thread->gc_status, MVMGCStatus_NONE,
        MVMGCStatus_INTERRUPT);
    if (tc->instance->num_user_threads) {
        MVMuint32 n = tc->instance->num_user_threads;
        MVMuint32 i;
        for (i = 0; i < n; i++)
            apr_atomic_cas32(&tc->instance->user_threads[i]->body.tc->gc_status,
                MVMGCStatus_NONE, MVMGCStatus_INTERRUPT);
    }
}

/* Called by the coordinator in order to arrange agreement between the threads
 * that GC is done. */
static void coordinate_finishing_gc(MVMThreadContext *tc, MVMuint8 gen) {
    MVMuint32 num_stolen = 0;
    
    GCORCH_LOG(tc, "Coordinating GC termination...\n");
    
    /* We may have stolen the work of a blocked thread. These always vote for
     * finishing GC. */
    if (tc->stolen_for_gc) {
        MVMuint32 i = 0;
        while (tc->stolen_for_gc[i]) {
            GCORCH_LOG(tc, "Incrementing thread GC count for a blocked/stolen thread\n");
            apr_atomic_inc32(&tc->instance->gc_orch->finish_votes);
            i++;
            num_stolen++;
        }
    }
    
    /* Now seek termination... */
    while (1) {
        MVMint8 termination_void = 0;
        
        /* Process our in-tray, and that of any stolen threads. */
        process_in_tray(tc, gen);
        if (tc->stolen_for_gc) {
            MVMuint32 i = 0;
            while (tc->stolen_for_gc[i]) {
                process_in_tray(tc->stolen_for_gc[i], gen);
                i++;
            }
        }
        
        /* See if all other threads have voted. */
        if ((tc->instance->gc_orch->expected_gc_threads - tc->instance->gc_orch->finish_votes) > 1)
            termination_void = 1;
        
        /* All voted but us, but are we really done? Check all the in-trays. */
        else if (tc->instance->main_thread->gc_in_tray) {
            termination_void = 1;
        }
        else {
            MVMuint32 i;
            for (i = 0; i < tc->instance->num_user_threads; i--) {
                if (tc->instance->user_threads[i]->body.tc->gc_in_tray) {
                    termination_void = 1;
                    break;
                }
            }
        }

        /* Check that no other thread started running GC again. */
        if ((tc->instance->gc_orch->expected_gc_threads - tc->instance->gc_orch->finish_votes) > 1)
            termination_void = 1;
        
        /* If termination wasn't voided, clean up after the run and do the
         * final vote. Also set number of threads that need to acknowledge
         * that we're done with GC. */
        if (!termination_void) {
            GCORCH_LOG(tc, "Coordinator decided GC is terminated\n");
            cleanup_all(tc);
            tc->instance->gc_orch->finish_ack_remaining = 
                tc->instance->gc_orch->expected_gc_threads -
                (1 + num_stolen);
            tc->instance->gc_orch->stage = MVM_gc_stage_finished;
            apr_atomic_inc32(&tc->instance->gc_orch->finish_votes);
            break;
        }
    }
}

/* Called by a thread to indicate it is about to enter a blocking operation.
 * This tells any thread that is coordinating a GC run that this thread will
 * be unable to participate. */
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc) {
    /* This may need more than one attempt. */
    while (1) {
        /* Try to set it from running to unable - the common case. */
        if (apr_atomic_cas32(&tc->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_NONE) == MVMGCStatus_NONE)
            return;
        
        /* The only way this can fail is if another thread just decided we're to
         * participate in a GC run. */
        if (tc->gc_status == MVMGCStatus_INTERRUPT)
            MVM_gc_enter_from_interrupt(tc);
        else
            MVM_panic(MVM_exitcode_gcorch, "Invalid GC status observed; aborting");
    }
}

/* Called by a thread to indicate it has completed a block operation and is
 * thus able to particpate in a GC run again. Note that this case needs some
 * special handling if it comes out of this mode when a GC run is taking place. */
void MVM_gc_mark_thread_unblocked(MVMThreadContext *tc) {
    /* Try to set it from unable to running. */
    while (apr_atomic_cas32(&tc->gc_status, MVMGCStatus_NONE,
            MVMGCStatus_UNABLE) != MVMGCStatus_UNABLE) {
        /* We can't, presumably because a GC run is going on. We should wait
         * for that to finish before we go on, but without chewing CPU. */
        apr_thread_yield();
    }
}

/* Called by a thread to indicate it is dying and needs reaped. */
void MVM_gc_mark_thread_dying(MVMThreadContext *tc) {
    /* This may need more than one attempt. */
    while (1) {
        /* Try to set it from running to dying - the common case. */
        if (apr_atomic_cas32(&tc->gc_status, MVMGCStatus_DYING,
                MVMGCStatus_NONE) == MVMGCStatus_NONE)
            return;
        
        /* The only way this can fail is if another thread just decided we're to
         * participate in a GC run. */
        if (tc->gc_status == MVMGCStatus_INTERRUPT)
            MVM_gc_enter_from_interrupt(tc);
        else
            MVM_panic(MVM_exitcode_gcorch, "Invalid GC status observed; aborting");
    }
}

/* This is called when the allocator finds it has run out of memory and wants
 * to trigger a GC run. In this case, it's possible (probable, really) that it
 * will need to do that triggering, notifying other running threads that the
 * time has come to GC. */
void MVM_gc_enter_from_allocator(MVMThreadContext *tc) {
    MVMGCOrchestration *old_gc_orch, *gc_orch;
    void               *limit;
    void              **stolen_limits;
    MVMuint8            gen, set_orch;
    
    GCORCH_LOG(tc, "Entered from allocate\n");
    
    /* Wait just in case other threads didn't agree we finished the
     * previous GC run yet. */
    old_gc_orch = tc->instance->gc_orch;
    set_orch = 0;
    if (old_gc_orch) {
        if (old_gc_orch->stage == MVM_gc_stage_finished) {
            GCORCH_LOG(tc, "Waiting for remaining to acknowledge last complete\n");
            while (old_gc_orch->finish_ack_remaining)
                apr_thread_yield();
        }
        if (old_gc_orch->stage == MVM_gc_stage_initializing) {
            GCORCH_LOG(tc, "Waiting for the thing to be initialized\n");
            while (old_gc_orch->stage == MVM_gc_stage_initializing);
        }
        gc_orch = old_gc_orch;
    }
    else {
        gc_orch = malloc(sizeof(MVMGCOrchestration));
        gc_orch->stage = MVM_gc_stage_initializing;
        if ((old_gc_orch = apr_atomic_casptr(&tc->instance->gc_orch, gc_orch, NULL)) != NULL) {
            free(gc_orch);
            gc_orch = old_gc_orch;
        }
        else {
            set_orch = 1;
        }
    }
    
    /* Try to start the GC run. */
    if (set_orch || apr_atomic_cas32(&gc_orch->stage, MVM_gc_stage_initializing, MVM_gc_stage_finished) == MVM_gc_stage_finished) {
        /* We are the winner of the GC starting race. This gives us some
         * extra responsibilities as well as doing the usual things.
         * First, increment GC sequence number. */
        GCORCH_LOG(tc, "GC thread elected coordinator\n");
        
        /* Grab the thread starting mutex while we start GC. This is so we
         * can get an accurate and stable number of threads that we expect to
         * join in with the GC. Store it in a new GC orchestration structure. */
        if (apr_thread_mutex_lock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcorch, "Unable to lock user_threads mutex");
        
        gc_orch->expected_gc_threads = tc->instance->num_user_threads + 1;
        
        /* Count us into the GC run from the start. */
        gc_orch->start_votes = 1;
        gc_orch->finish_votes = 0;
        gc_orch->finish_ack_remaining = 0;
        tc->instance->gc_seq_number++;
        
        /* Ensure our stolen list is empty. */
        if (tc->stolen_for_gc) {
            free(tc->stolen_for_gc);
            tc->stolen_for_gc = NULL;
        }
        
        /* set the state of the gc_orch object to unfinished. */
        gc_orch->stage = MVM_gc_stage_started;
        
        /* Signal other threads to do a GC run. */
        signal_all_but(tc);
        
        /* Now that we've signalled all threads we expect to join in,
         * we can safely release the thread starting mutex. */
        if (apr_thread_mutex_unlock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcorch, "Unable to unlock user_threads mutex");
        
        /* Wait for all thread to indicate readiness to collect. */
        wait_for_all_threads(tc, tc->instance);
        
        /* Do GC work for this thread, or at least all we know about. */
        gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
            ? MVMGCGenerations_Both
            : MVMGCGenerations_Nursery;
        limit = tc->nursery_alloc;
        MVM_gc_collect(tc, MVMGCWhatToDo_All, gen);
        
        /* Do GC work for any stolen threads. */
        if (tc->stolen_for_gc) {
            MVMuint32 i = 0, n = 0;
            while (tc->stolen_for_gc[i++])
                n++;
            stolen_limits = malloc(n * sizeof(void *));
            for (i = 0; i < n; i++) {
                stolen_limits[i] = tc->stolen_for_gc[i]->nursery_alloc;
                MVM_gc_collect(tc->stolen_for_gc[i],
                    MVMGCWhatToDo_NoInstance, gen);
            }
        }

        /* Try to get everybody to agree we're done. */
        coordinate_finishing_gc(tc, gen);

        /* Now we're all done, it's safe to finalize any objects that need it. */
        MVM_gc_collect_free_nursery_uncopied(tc, limit);
        if (gen == MVMGCGenerations_Both)
            MVM_gc_collect_free_gen2_unmarked(tc);
        if (tc->stolen_for_gc) {
            MVMuint32 i = 0;
            while (tc->stolen_for_gc[i]) {
                MVM_gc_collect_free_nursery_uncopied(tc->stolen_for_gc[i], stolen_limits[i]);
                if (gen == MVMGCGenerations_Both)
                    MVM_gc_collect_free_gen2_unmarked(tc->stolen_for_gc[i]);
                i++;
            }
            free(stolen_limits);
        }
    }
    else {
        /* Another thread beat us to starting the GC sync process. Thus, act as
         * if we were interrupted to GC. */
        MVM_gc_enter_from_interrupt(tc);
    }
}

/* This is called when a thread hits an interrupt at a GC safe point. This means
 * that another thread is already trying to start a GC run, so we don't need to
 * try and do that, just enlist in the run. */
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc) {
    void *limit;
    MVMuint8 gen;

    while (tc->instance->gc_orch->stage != MVM_gc_stage_started);
    
    /* Count us in to the GC run. */
    GCORCH_LOG(tc, "Entered from interrupt\n");
    apr_atomic_inc32(&tc->instance->gc_orch->start_votes);
    
    /* Ensure our stolen list is empty. */
    if (tc->stolen_for_gc) {
        free(tc->stolen_for_gc);
        tc->stolen_for_gc = NULL;
    }

    /* Wait for all thread to indicate readiness to collect. */
    wait_for_all_threads(tc, tc->instance);
    
    /* Do GC work for this thread, or at least all we know about. */
    gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
        ? MVMGCGenerations_Both
        : MVMGCGenerations_Nursery;
    limit = tc->nursery_alloc;
    MVM_gc_collect(tc, MVMGCWhatToDo_NoInstance, gen);

    /* Wait for completion, doing any extra work that we need to. */
    finish_gc(tc, gen);

    /* Now we're all done, it's safe to finalize any objects that need it. */
    MVM_gc_collect_free_nursery_uncopied(tc, limit);
    if (gen == MVMGCGenerations_Both)
        MVM_gc_collect_free_gen2_unmarked(tc);
}
