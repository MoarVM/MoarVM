#include "moarvm.h"

#define GCORCH_DEGUG 1
#define GCORCH_LOG(tc, msg, ...) if (GCORCH_DEGUG) printf(msg, tc->thread_id, __VA_ARGS__)

/* If we steal the job of doing GC for a thread, we add it to our stolen
 * list. */
static void add_stolen(MVMThreadContext *us, MVMThreadContext *stolen) {
    MVMGCOrchestration *gc_orch = us->instance->gc_orch;
    if (gc_orch->stolen == NULL) {
        gc_orch->stolen_size = 16;
        gc_orch->stolen = malloc(gc_orch->stolen_size * sizeof(MVMThreadContext *));
    }
    else if (gc_orch->stolen_count == gc_orch->stolen_size) {
        gc_orch->stolen_size *= 2;
        gc_orch->stolen = realloc(gc_orch->stolen, gc_orch->stolen_size * sizeof(MVMThreadContext *));
    }
    gc_orch->stolen[gc_orch->stolen_count++] = stolen;
}

/* Goes through all threads but the current one and notifies them that a
 * GC run is starting. Those that are blocked are considered excluded from
 * the run, but still counted. */
static MVMuint32 signal_one_thread(MVMThreadContext *us, MVMThreadContext *to_signal) {
    /* Loop here since we may not succeed first time (e.g. the status of the
     * thread may change between the two ways we try to twiddle it). */
    while (1) {
        /* If it's already set itself to interrupt, count it. */
        if (to_signal->gc_status == MVMGCStatus_INTERRUPT) {
            GCORCH_LOG(us, "Thread %d : thread %d interrupted itself\n", to_signal->thread_id);
            return 1;
        }
        
        if (to_signal->gc_status != MVMGCStatus_NONE
                && to_signal->gc_status != MVMGCStatus_UNABLE
                && to_signal->gc_status != MVMGCStatus_DYING) {
            MVM_panic(3, "thread in strange state: %d\n", to_signal->gc_status);
        }
        
        /* Try to set it from running to interrupted - the common case. */
        if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_INTERRUPT,
                MVMGCStatus_NONE) == MVMGCStatus_NONE) {
            GCORCH_LOG(us, "Thread %d : Signalled thread %d to interrupt\n", to_signal->thread_id);
            return 1;
        }
        
        /* Otherwise, it's blocked; try to set it to work stolen. */
        if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_STOLEN,
                MVMGCStatus_UNABLE) == MVMGCStatus_UNABLE
           || apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_REAPING,
                MVMGCStatus_DYING) == MVMGCStatus_DYING) {
            /* We stole the work; it's now sufficiently opted in to GC that
             * we can increment the count of threads that are opted in. */
            add_stolen(us, to_signal);
            MVM_atomic_decr(&to_signal->instance->gc_orch->start_votes_remaining);
            GCORCH_LOG(us, "Thread %d : A blocked thread %d spotted; work stolen\n", to_signal->thread_id);
            return 1;
        }
    }
}
static MVMuint32 signal_all_but(MVMThreadContext *tc) {
    MVMInstance *ins = tc->instance;
    MVMuint32 i;
    MVMuint32 count = 0;
    MVMThread *t = tc->instance->running_threads;
    MVMThread *next, *starting_list = NULL;
  again:
    do {
        next = t->body.next;
        switch (t->body.stage) {
            case MVM_thread_stage_starting:
                GCORCH_LOG(tc, "Thread %d : Detected starting thread %d\n", t->body.tc->thread_id);
                /* ignore; it will detect the gc run and wait. */
                break;
            case MVM_thread_stage_waiting:
                GCORCH_LOG(tc, "Thread %d : Detected starting thread %d waiting for interrupt\n", t->body.tc->thread_id);
                /* fall through */
            case MVM_thread_stage_started:
                if (t->body.tc != tc) {
                    count += signal_one_thread(tc, t->body.tc);
                }
                break;
            case MVM_thread_stage_exited:
                GCORCH_LOG(tc, "Thread %d : Destroying thread %d\n", t->body.tc->thread_id);
                MVM_tc_destroy(t->body.tc);
                t->body.tc = NULL;
                t->body.stage = MVM_thread_stage_destroyed;
                break;
            case MVM_thread_stage_destroyed:
                /* will be cleaned up shortly */
                break;
            default:
                MVM_panic(MVM_exitcode_gcorch, "Corrupted MVMThread or running threads list: invalid thread stage %d", t->body.stage);
        }
    } while (t = next);
    if (!starting_list && (t = starting_list = tc->instance->starting_threads)) goto again;
    return count;
}

/* Waits for all threads to have enlisted in the GC run. For now, just stupid
 * spinning. */
static void wait_for_all_threads(MVMThreadContext *tc, MVMInstance *i, AO_t threshold) {
    GCORCH_LOG(tc, "Thread %d : Waiting for all %u threads...\n", i->gc_orch->start_votes_remaining);
    while (i->gc_orch->start_votes_remaining != threshold)
        1;
    GCORCH_LOG(tc, "Thread %d : All threads now registered for the GC run\n");
}

/* Does work in a thread's in-tray, if any. */
static void process_in_tray(MVMThreadContext *tc, MVMuint8 gen, MVMuint32 *put_vote) {
    /* Do we have any more work given by another thread? If so, re-enter
     * GC loop to process it. Note that since we're now doing GC stuff
     * again, we take back our vote to finish. */
    if (tc->gc_in_tray) {
        GCORCH_LOG(tc, "Thread %d : Was given extra work by another thread; doing it\n");
        if (!*put_vote) {
            MVM_atomic_incr(&tc->instance->gc_orch->finish_votes_remaining);
            *put_vote = 1;
        }
        MVM_gc_collect(tc, MVMGCWhatToDo_InTray, gen);
    }
}

/* Does work in a thread's in-tray, if any. Returns 0 if all work is done. */
static MVMuint32 process_sent_items(MVMThreadContext *tc, MVMuint32 *put_vote) {
    /* Is any of our work outstanding? If so, take away our finish vote.
     * If we successfully check all our work, add the finish vote back. */
    MVMGCPassedWork *work = tc->gc_next_to_check, *last;
    MVMuint32 advanced = 0;
    if (work) {
        if (!*put_vote) {
            MVM_atomic_incr(&tc->instance->gc_orch->finish_votes_remaining);
            *put_vote = 1;
        }
        /* if we have a submitted work item we haven't claimed a vote for, get a vote. */
        if (!work->upvoted) {
            work->upvoted = 1;
        }
        while (work->completed && (advanced = 1) && (work = work->next_by_sender));
        if (advanced) {
            tc->gc_next_to_check = work;
        }
        /* if all our submitted work items are completed, release the vote. */
        if (!work) {
            return 0;
        }
        else {
            work->upvoted = 1;
            return 1;
        }
    }
    return 0;
}

/* Called by a thread when it thinks it is done with GC. It may get some more
 * work yet, though. Called by all threads except the co-ordinator. */
static void finish_gc(MVMThreadContext *tc, MVMuint8 gen, AO_t threshold, MVMuint8 process_stolen) {
    MVMGCOrchestration *gc_orch = tc->instance->gc_orch;
    MVMuint32 put_vote = 1;
    
    /* Loop until other threads have terminated, processing any extra work
     * that we are given. The coordinator decrements its count last, which
     * is how we know all is over. */
    while (gc_orch->finish_votes_remaining != threshold) {
        MVMuint32 failed = 0;
        process_in_tray(tc, gen, &put_vote);
        failed = process_sent_items(tc, &put_vote) || failed;
        if (process_stolen && gc_orch->stolen_count) {
            MVMuint32 i = 0;
            for ( ; i < gc_orch->stolen_count; i++) {
                process_in_tray(gc_orch->stolen[i], gen, &put_vote);
                failed = process_sent_items(gc_orch->stolen[i], &put_vote) || failed;
            }
        }
        if (!failed && put_vote) {
            MVM_atomic_decr(&gc_orch->finish_votes_remaining);
            put_vote = 0;
        }
    }
    GCORCH_LOG(tc, "Thread %d : Discovered GC termination\n");
}

/* Cleans up after a GC run, resetting flags and so forth. */
static void cleanup_all(MVMThreadContext *tc) {
    /* Reset GC status flags for any stolen threads. */
    MVMGCOrchestration *gc_orch = tc->instance->gc_orch;
    if (gc_orch->stolen_count) {
        MVMuint32 i = 0;
        for ( ; i < gc_orch->stolen_count; i++) {
            apr_atomic_cas32(&gc_orch->stolen[i]->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_STOLEN);
            apr_atomic_cas32(&gc_orch->stolen[i]->gc_status, MVMGCStatus_REAPED,
                MVMGCStatus_REAPING);
        }
    }
    
    /* Reset status for all other threads. */
    {
        MVMThread *t = tc->instance->starting_threads;
        while (t) {
            if (t->body.tc->gc_status == MVMGCStatus_INTERRUPT) {
                apr_atomic_cas32(&t->body.tc->gc_status,
                    MVMGCStatus_NONE, MVMGCStatus_INTERRUPT);
            }
            t = t->body.next;
        }
        t = tc->instance->running_threads;
        do {
            if (t->body.tc->gc_status == MVMGCStatus_INTERRUPT) {
                apr_atomic_cas32(&t->body.tc->gc_status,
                    MVMGCStatus_NONE, MVMGCStatus_INTERRUPT);
            }
        } while (t = t->body.next);
    }
}

/* Called by the coordinator in order to arrange agreement between the threads
 * that GC is done. */
static void coordinate_finishing_gc(MVMThreadContext *tc, MVMuint8 gen) {
    MVMGCOrchestration *gc_orch = tc->instance->gc_orch;
    
    GCORCH_LOG(tc, "Thread %d : Coordinating GC termination...\n");
    
    /* Wait for completion, doing any extra work that we need to. */
    finish_gc(tc, gen, 1, 1);
    gc_orch->start_votes_remaining = (MVMuint32)0-1;
    
    GCORCH_LOG(tc, "Thread %d : Coordinator decided GC is terminated\n");
    cleanup_all(tc);
    tc->instance->gc_orch->stage = MVM_gc_stage_finished;
    {
        AO_t old_vote_count = MVM_atomic_decr(&tc->instance->gc_orch->finish_votes_remaining);
        GCORCH_LOG(tc, sizeof(AO_t) == 8 
            ? "Thread %d : Signalling to other threads finished with GC: decremented finish_votes_remaining to %lu\n"
            : "Thread %d : Signalling to other threads finished with GC: decremented finish_votes_remaining to %u\n", old_vote_count - 1);
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
    GCORCH_LOG(tc, "Thread %d : Marking self dying!\n");
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
    MVMGCOrchestration *gc_orch;
    void               *limit;
    void              **stolen_limits;
    MVMuint8            gen, set_orch;
    
    GCORCH_LOG(tc, "Thread %d : Entered from allocate\n");
    
    /* Wait just in case other threads didn't agree we finished the
     * previous GC run yet. */
    gc_orch = tc->instance->gc_orch;
    set_orch = 0;
    if (gc_orch) {
        /*if (old_gc_orch->stage == MVM_gc_stage_finished) {
            GCORCH_LOG(tc, "Thread %d : Waiting for remaining to acknowledge last complete\n");
            while (old_gc_orch->finish_ack_remaining)
                apr_thread_yield();
        if (gc_orch->stage != MVM_gc_stage_finished) {
            GCORCH_LOG(tc, "Thread %d : Waiting for the last run to complete\n", tc->thread_id);
            while (gc_orch->stage != MVM_gc_stage_finished);
        }
        */
    }
    else {
        gc_orch = malloc(sizeof(MVMGCOrchestration));
        gc_orch->stage = MVM_gc_stage_initializing;
        /* must initialize all of these here in case another thread enters from
         * allocate immediately after we CAS it into place, so when it adds itself
         * to start_votes and starts comparing against expected, things go ok. */
        gc_orch->stolen = NULL;
        gc_orch->start_votes_remaining = (MVMuint32)0-1;
        if (apr_atomic_casptr(&tc->instance->gc_orch, gc_orch, NULL) != NULL) {
            free(gc_orch);
            gc_orch = tc->instance->gc_orch;
        }
        else {
            set_orch = 1;
        }
    }
    
    /* Try to start the GC run. */
    if (set_orch || apr_atomic_cas32(&gc_orch->stage, MVM_gc_stage_initializing, MVM_gc_stage_finished) == MVM_gc_stage_finished) {
        MVMuint32 num_threads;
        /* We are the winner of the GC starting race. This gives us some
         * extra responsibilities as well as doing the usual things.
         * First, increment GC sequence number. */
        
        tc->instance->gc_seq_number++;
        GCORCH_LOG(tc, "Thread %d : GC thread elected coordinator: starting gc seq %d\n", tc->instance->gc_seq_number);
        
        /* Ensure our stolen list is empty. */
        gc_orch->stolen_count = 0;
        
        /* Signal other threads to do a GC run. */
        num_threads = signal_all_but(tc);
        /* one for our main vote, and one for the supplementary vote */
        gc_orch->start_votes_remaining = num_threads;
        GCORCH_LOG(tc, "Thread %d : set num_threads to %d\n", num_threads);
        GCORCH_LOG(tc, "Thread %d : stolen_count is %d\n", gc_orch->stolen_count);
        
        MVM_barrier();
        
        /* set the state of the gc_orch object to unfinished,
         * signalling to all other threads it's safe to start the run. */
        gc_orch->stage = MVM_gc_stage_started;
        
        /* Wait for all other threads to indicate readiness to collect. */
        wait_for_all_threads(tc, tc->instance, 1);
        gc_orch->finish_votes_remaining = num_threads - gc_orch->stolen_count + 2;
        
        /* cleanup the lists */
        MVM_thread_cleanup_threads_list(tc, &tc->instance->starting_threads);
        MVM_thread_cleanup_threads_list(tc, &tc->instance->running_threads);
        
        /* signal to the rest to start */
        MVM_atomic_decr(&gc_orch->start_votes_remaining);
        
        /* Do GC work for this thread, or at least all we know about. */
        gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
            ? MVMGCGenerations_Both
            : MVMGCGenerations_Nursery;
        limit = tc->nursery_alloc;
        MVM_gc_collect(tc, MVMGCWhatToDo_All, gen);
        
        /* Do GC work for any stolen threads. */
        if (gc_orch->stolen_count) {
            MVMuint32 i = 0, n = gc_orch->stolen_count;
            stolen_limits = malloc(n * sizeof(void *));
            for ( ; i < n; i++) {
                stolen_limits[i] = gc_orch->stolen[i]->nursery_alloc;
                MVM_gc_collect(gc_orch->stolen[i], MVMGCWhatToDo_NoInstance, gen);
            }
        }
        
        /* Try to get everybody to agree we're done. */
        coordinate_finishing_gc(tc, gen);
        
        /* Now we're all done, it's safe to finalize any objects that need it. */
        MVM_gc_collect_free_nursery_uncopied(tc, limit);
        if (gen == MVMGCGenerations_Both)
            MVM_gc_collect_free_gen2_unmarked(tc);
        if (gc_orch->stolen_count) {
            MVMuint32 i = 0, n = gc_orch->stolen_count;
            for ( ; i < n; i++) {
                MVM_gc_collect_free_nursery_uncopied(gc_orch->stolen[i], stolen_limits[i]);
                if (gen == MVMGCGenerations_Both)
                    MVM_gc_collect_free_gen2_unmarked(gc_orch->stolen[i]);
            }
            free(stolen_limits);
        }
    }
    else {
        /* Another thread beat us to starting the GC sync process. Thus, act as
         * if we were interrupted to GC. */
        tc->gc_status =  MVMGCStatus_INTERRUPT;
        MVM_gc_enter_from_interrupt(tc);
    }
}

/* This is called when a thread hits an interrupt at a GC safe point. This means
 * that another thread is already trying to start a GC run, so we don't need to
 * try and do that, just enlist in the run. */
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc) {
    void *limit;
    MVMuint8 gen;
    
    /* stage is always set to finished before any thread finishes its gc run.
     * Basically we are waiting for start_votes_remaining to be set. */
    if (tc->instance->gc_orch->stage != MVM_gc_stage_started) {
        GCORCH_LOG(tc, "Thread %d : Waiting for the last run and initialization to complete\n");
        while (tc->instance->gc_orch->stage != MVM_gc_stage_started);
    }
    
    /* Count us in to the GC run. */
    GCORCH_LOG(tc, "Thread %d : Entered from interrupt\n");
    MVM_atomic_decr(&tc->instance->gc_orch->start_votes_remaining);
    
    /* Wait for all thread to indicate readiness to collect. */
    wait_for_all_threads(tc, tc->instance, 0);
    
    /* Do GC work for this thread, or at least all we know about. */
    gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
        ? MVMGCGenerations_Both
        : MVMGCGenerations_Nursery;
    limit = tc->nursery_alloc;
    MVM_gc_collect(tc, MVMGCWhatToDo_NoInstance, gen);
    
    /* Wait for completion, doing any extra work that we need to. */
    finish_gc(tc, gen, 0, 0);
    
    /* Now we're all done, it's safe to finalize any objects that need it. */
    MVM_gc_collect_free_nursery_uncopied(tc, limit);
    if (gen == MVMGCGenerations_Both)
        MVM_gc_collect_free_gen2_unmarked(tc);
}
