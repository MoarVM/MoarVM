#include "moarvm.h"

#define GCORCH_DEBUG 1
#ifdef _MSC_VER
# define GCORCH_LOG(tc, msg, ...) if (GCORCH_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number, __VA_ARGS__)
#else
# define GCORCH_LOG(tc, msg, ...) if (GCORCH_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number , # __VA_ARGS__)
#endif

/* never this many threads will collide in enter_from_allocate at once */
#define NEVER_THIS_MANY 10000000

/* If we steal the job of doing GC for a thread, we add it to our stolen
 * list. */
static void add_stolen(MVMThreadContext *tc, MVMThreadContext *stolen) {
    if (tc->gc_stolen == NULL) {
        tc->gc_stolen_size = 16;
        tc->gc_stolen = malloc(tc->gc_stolen_size * sizeof(MVMStolenThread));
    }
    else if (tc->gc_stolen_count == tc->gc_stolen_size) {
        tc->gc_stolen_size *= 2;
        tc->gc_stolen = realloc(tc->gc_stolen, tc->gc_stolen_size * sizeof(MVMStolenThread));
    }
    tc->gc_stolen[tc->gc_stolen_count++].tc = stolen;
}

/* Goes through all threads but the current one and notifies them that a
 * GC run is starting. Those that are blocked are considered excluded from
 * the run, but still counted. Returns the count of threads that should be
 * added to the finished countdown. */
static MVMuint32 signal_one_thread(MVMThreadContext *tc, MVMThreadContext *to_signal) {
    
    /* Loop here since we may not succeed first time (e.g. the status of the
     * thread may change between the two ways we try to twiddle it). */
    while (1) {
        switch (to_signal->gc_status) {
            case MVMGCStatus_NONE:
                /* Try to set it from running to interrupted - the common case. */
                if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_INTERRUPT,
                        MVMGCStatus_NONE) == MVMGCStatus_NONE) {
//                    GCORCH_LOG(tc, "Thread %d run %d : Signalled thread %d to interrupt\n", to_signal->thread_id);
                    MVM_atomic_incr(&tc->instance->gc_start);
                    return 1;
                }
                break;
            /* this case shouldn't occur */
            case MVMGCStatus_INTERRUPT:
//                GCORCH_LOG(tc, "Thread %d run %d : thread %d already interrupted\n", to_signal->thread_id);
                MVM_panic(MVM_exitcode_gcorch, "invalid GC status");
            case MVMGCStatus_UNABLE:
                /* Otherwise, it's blocked; try to set it to work stolen. */
                if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_STOLEN,
                        MVMGCStatus_UNABLE) == MVMGCStatus_UNABLE) {
//                    GCORCH_LOG(tc, "Thread %d run %d : A blocked thread %d spotted; work stolen\n", to_signal->thread_id);
                    add_stolen(tc, to_signal);
                    return 0;
                }
                break;
            /* this case occurs if a child thread is stolen by its parent
             * before we get to it in the chain. */
            case MVMGCStatus_STOLEN:
//                GCORCH_LOG(tc, "Thread %d run %d : thread %d already stolen (it was a spawning child)\n", to_signal->thread_id);
                return 0;
            default:
                MVM_panic(MVM_exitcode_gcorch, "invalid status %d in GC orchestrate\n", to_signal->gc_status);
                return 0;
        }
    }
}
static MVMuint32 signal_all_but(MVMThreadContext *tc, MVMThread *t) {
    MVMInstance *ins = tc->instance;
    MVMuint32 i;
    MVMuint32 count = 0;
    MVMThread *next;
    if (!t) {
        return 0;
    }
    do {
        next = t->body.next;
        switch (t->body.stage) {
            case MVM_thread_stage_starting:
            case MVM_thread_stage_waiting:
            case MVM_thread_stage_started:
                if (t->body.tc != tc) {
//                    GCORCH_LOG(tc, "Thread %d run %d : sending thread %d\n", t->body.tc->thread_id);
                    count += signal_one_thread(tc, t->body.tc);
                }
                break;
            case MVM_thread_stage_exited:
                GCORCH_LOG(tc, "Thread %d run %d : Destroying thread %d\n", t->body.tc->thread_id);
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
//        GCORCH_LOG(tc, "Thread %d run %d : gc_start is now %d\n", tc->instance->gc_start);
    } while (t = next);
    return count;
}

/* Waits for all threads to have enlisted in the GC run. For now, just stupid
 * spinning. */
static void wait_for_all_threads(MVMThreadContext *tc, AO_t threshold) {
    MVMInstance *i = tc->instance;
    MVMuint32 rem = i->gc_start;
//    GCORCH_LOG(tc, "Thread %d run %d : Waiting for all %u threads...\n",
//        rem > NEVER_THIS_MANY / 2 ? rem - NEVER_THIS_MANY : rem);
    while (i->gc_start != threshold)
        1;
//    GCORCH_LOG(tc, "Thread %d run %d : All threads now registered for the GC run\n");
}

/* Does work in a thread's in-tray, if any. */
static void process_in_tray(MVMThreadContext *tc, MVMuint8 gen, MVMuint32 *put_vote) {
    /* Do we have any more work given by another thread? If so, re-enter
     * GC loop to process it. Note that since we're now doing GC stuff
     * again, we take back our vote to finish. */
    if (tc->gc_in_tray) {
//        GCORCH_LOG(tc, "Thread %d run %d : Was given extra work by another thread; doing it\n");
        if (!*put_vote) {
            MVM_atomic_incr(&tc->instance->gc_finish);
            *put_vote = 1;
        }
        MVM_gc_collect(tc, MVMGCWhatToDo_InTray, gen);
    }
}

/* Checks whether our sent items have been completed. Returns 0 if all work is done. */
static MVMuint32 process_sent_items(MVMThreadContext *tc, MVMuint32 *put_vote) {
    /* Is any of our work outstanding? If so, take away our finish vote.
     * If we successfully check all our work, add the finish vote back. */
    MVMGCPassedWork *work = tc->gc_next_to_check;
    MVMuint32 advanced = 0;
    if (work) {
        /* if we have a submitted work item we haven't claimed a vote for, get a vote. */
        if (!*put_vote) {
            MVM_atomic_incr(&tc->instance->gc_finish);
            *put_vote = 1;
        }
        while (work->completed) {
            advanced = 1;
            work = work->next_by_sender;
            if (!work) break;
        }
        if (advanced)
            tc->gc_next_to_check = work;
        /* if all our submitted work items are completed, release the vote. */
        /* otherwise indicate that something we submitted isn't finished */
        return work ? (work->upvoted = 1) : 0;
    }
    return 0;
}

static void cleanup_sent_items(MVMThreadContext *tc) {
    MVMGCPassedWork *work, *next = tc->gc_sent_items;
    while (work = next) {
        next = work->last_by_sender;
        free(work);
    }
    tc->gc_sent_items = NULL;
}

/* Called by a thread when it thinks it is done with GC. It may get some more
 * work yet, though. */
static void finish_gc(MVMThreadContext *tc, MVMuint8 gen) {
    MVMuint32 put_vote = 1;
    
    /* Loop until other threads have terminated, processing any extra work
     * that we are given. The coordinator decrements its count last, which
     * is how we know all is over. */
    while (tc->instance->gc_finish) {
        MVMuint32 failed = 0;
        process_in_tray(tc, gen, &put_vote);
        failed = process_sent_items(tc, &put_vote) | failed;
        
        if (tc->gc_stolen_count) {
            MVMuint32 i = 0;
            for ( ; i < tc->gc_stolen_count; i++) {
                process_in_tray(tc->gc_stolen[i].tc, gen, &put_vote);
                failed = process_sent_items(tc->gc_stolen[i].tc, &put_vote) | failed;
            }
        }
        if (!failed && put_vote) {
            MVM_atomic_decr(&tc->instance->gc_finish);
            put_vote = 0;
        }
    }
//    GCORCH_LOG(tc, "Thread %d run %d : Discovered GC termination\n");
    
    cleanup_sent_items(tc);
    
    /* Reset GC status flags and cleanup sent items for any stolen threads. */
    if (tc->gc_stolen_count) {
        MVMuint32 i = 0;
        for ( ; i < tc->gc_stolen_count; i++) {
            cleanup_sent_items(tc->gc_stolen[i].tc);
            apr_atomic_cas32(&tc->gc_stolen[i].tc->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_STOLEN);
        }
    }
    apr_atomic_cas32(&tc->gc_status, MVMGCStatus_NONE, MVMGCStatus_INTERRUPT);
    MVM_atomic_decr(&tc->instance->gc_ack);
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

static void run_gc(MVMThreadContext *tc, MVMuint8 what_to_do) {
    MVMuint8   gen;
    void      *limit;
    MVMThread *child;
    
    /* if we still have it, its state will be UNABLE, so steal it. */
    if (child = tc->thread_obj->body.new_child) {
        signal_one_thread(tc, child->body.tc);
        tc->thread_obj->body.new_child = NULL;
    }
    
    /* Do GC work for this thread, or at least all we know about. */
    gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
        ? MVMGCGenerations_Both
        : MVMGCGenerations_Nursery;
    limit = tc->nursery_alloc;
    GCORCH_LOG(tc, "Thread %d run %d : starting own collection\n");
    MVM_gc_collect(tc, what_to_do, gen);
    
    /* Do GC work for any stolen threads. */
    if (tc->gc_stolen_count) {
        MVMuint32 i = 0, n = tc->gc_stolen_count;
        for ( ; i < n; i++) {
            tc->gc_stolen[i].limit = tc->gc_stolen[i].tc->nursery_alloc;
            GCORCH_LOG(tc, "Thread %d run %d : starting stolen collection for thread %d\n",
                tc->gc_stolen[i].tc->thread_id);
            MVM_gc_collect(tc->gc_stolen[i].tc, MVMGCWhatToDo_NoInstance, gen);
        }
    }
    
    /* Wait for everybody to agree we're done. */
    finish_gc(tc, gen);
    
    /* Now we're all done, it's safe to finalize any objects that need it. */
    MVM_gc_collect_free_nursery_uncopied(tc, limit);
    if (gen == MVMGCGenerations_Both)
        MVM_gc_collect_free_gen2_unmarked(tc);
    
    if (tc->gc_stolen_count) {
        MVMuint32 i = 0, n = tc->gc_stolen_count;
        for ( ; i < n; i++) {
            MVM_gc_collect_free_nursery_uncopied(tc->gc_stolen[i].tc, tc->gc_stolen[i].limit);
            if (gen == MVMGCGenerations_Both)
                MVM_gc_collect_free_gen2_unmarked(tc->gc_stolen[i].tc);
        }
    }
}

/* This is called when the allocator finds it has run out of memory and wants
 * to trigger a GC run. In this case, it's possible (probable, really) that it
 * will need to do that triggering, notifying other running threads that the
 * time has come to GC. */
void MVM_gc_enter_from_allocator(MVMThreadContext *tc) {
    
//    GCORCH_LOG(tc, "Thread %d run %d : Entered from allocate\n");
    
    /* Try to start the GC run. */
    if (MVM_cas(&tc->instance->gc_start, 0, NEVER_THIS_MANY)) {
        MVMThread *last_starter = NULL;
        MVMuint32 num_threads = 0;
        
        /* We are the winner of the GC starting race. This gives us some
         * extra responsibilities as well as doing the usual things.
         * First, increment GC sequence number. */
        tc->instance->gc_seq_number++;
//        GCORCH_LOG(tc, "Thread %d run %d : GC thread elected coordinator: starting gc seq %d\n", tc->instance->gc_seq_number);
        
        /* Ensure our stolen list is empty. */
        tc->gc_stolen_count = 0;
        
//    GCORCH_LOG(tc, "Thread %d run %d : gc_ack was %d\n", tc->instance->gc_ack);
        /* need to wait for other threads to reset their gc_status. */
        while (tc->instance->gc_ack)
            apr_thread_yield();
        
        while (tc->instance->gc_start != NEVER_THIS_MANY
                || last_starter != tc->instance->starting_threads) {
            last_starter = tc->instance->starting_threads;
            /* Signal other threads to do a GC run and count how many will finish. */
            num_threads += signal_all_but(tc, tc->instance->running_threads)
                        + signal_all_but(tc, last_starter);
    //    GCORCH_LOG(tc, "Thread %d run %d : num_threads was %d\n", num_threads);
            
            /* Wait for all other threads to indicate readiness to collect. */
            while (tc->instance->gc_start > NEVER_THIS_MANY);
        /* at this point, there is a possibility more threads have launched, so we need
         * to check and rerun the signaling. */
        }
        
//    GCORCH_LOG(tc, "Thread %d run %d : gc_ack is now %d\n", tc->instance->gc_ack);
        
        if (tc->instance->gc_finish != 0)
            MVM_panic(MVM_exitcode_gcorch, "finish votes was %d\n", tc->instance->gc_finish);
        tc->instance->gc_finish = num_threads + 1;
//    GCORCH_LOG(tc, "Thread %d run %d : set gc_finish to %d\n", tc->instance->gc_finish);
        tc->instance->gc_ack = num_threads + 1;
        
        /* cleanup the lists */
    //    MVM_thread_cleanup_threads_list(tc, &tc->instance->starting_threads);
    //    MVM_thread_cleanup_threads_list(tc, &tc->instance->running_threads);
        
        MVM_barrier();
        
        /* signal to the rest to start */
        tc->instance->gc_start = 0;
        
        run_gc(tc, MVMGCWhatToDo_All);
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
    
    /* Count us in to the GC run. */
//    GCORCH_LOG(tc, "Thread %d run %d : Entered from interrupt\n");
    MVM_atomic_decr(&tc->instance->gc_start);
    
    tc->gc_stolen_count = 0;
    
    /* Wait for all threads to indicate readiness to collect. */
    wait_for_all_threads(tc, 0);
    
    run_gc(tc, MVMGCWhatToDo_NoInstance);
}
