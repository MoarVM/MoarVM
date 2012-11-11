#include "moarvm.h"

#define GCORCH_DEGUG 0
#define GCORCH_LOG(x) if (GCORCH_DEGUG) printf(x)

/* If we steal the job of doing GC for a thread, we add it to our stolen
 * list. */
static void add_stolen(MVMThreadContext *us, MVMThreadContext *stolen) {
    MVMuint32 i = 0;
    if (us->stolen_for_gc == NULL) {
        us->stolen_for_gc = malloc(us->instance->expected_gc_threads * sizeof(MVMThreadContext *));
        memset(us->stolen_for_gc, 0, us->instance->expected_gc_threads * sizeof(MVMThreadContext *));
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
                MVMGCStatus_UNABLE) == MVMGCStatus_UNABLE) {
            /* We stole the work; it's now sufficiently opted in to GC that
             * we can increment the count of threads that are opted in. */
            add_stolen(us, to_signal);
            apr_atomic_inc32(&to_signal->instance->starting_gc);
            GCORCH_LOG("A blocked thread spotted; work stolen\n");
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
static void wait_for_all_threads(MVMInstance *i) {
    GCORCH_LOG("Waiting for all threads...\n");
    while (i->starting_gc != i->expected_gc_threads)
        1;
    GCORCH_LOG("All threads now registered for the GC run\n");
}

/* Does work in a thread's in-tray, if any. */
static void process_in_tray(MVMThreadContext *tc) {
    /* Do we have any more work given by another thread? If so, re-enter
     * GC loop to process it. Note that since we're now doing GC stuff
     * again, we bump the count. */
    if (tc->gc_in_tray) {
        GCORCH_LOG("Was given extra work by another thread; doing it\n");
        apr_atomic_inc32(&tc->instance->starting_gc);
        MVM_gc_collect(tc, MVMGCWhatToDo_InTray);
        apr_atomic_dec32(&tc->instance->starting_gc);
    }
}

/* Called by a thread when it thinks it is done with GC. It may get some more
 * work yet, though. Called by all threads except the co-ordinator. */
static void finish_gc(MVMThreadContext *tc) {
    /* Decrement number of threads in GC. */
    GCORCH_LOG("Waiting for GC termination...\n");
    apr_atomic_dec32(&tc->instance->starting_gc);
    
    /* Loop until other threads have terminated, processing any extra work
     * that we are given. The coordinator decrements its count last, which
     * is how we know all is over. */
    while (tc->instance->starting_gc > 0) {
        process_in_tray(tc);
    }
}

/* Cleans up after a GC run, resetting flags and so forth. */
static void cleanup_all(MVMThreadContext *tc) {
    /* Reset GC status flags for any stolen threads. */
    if (tc->stolen_for_gc) {
        MVMuint32 i = 0;
        while (tc->stolen_for_gc[i]) {
            apr_atomic_cas32(&tc->stolen_for_gc[i]->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_STOLEN);
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
    
    /* Clear the expected GC counter, to allow future runs. */
    tc->instance->expected_gc_threads = 0;
}

/* Called by the coordinator in order to arrange agreement between the threads
 * that GC is done. */
static void coordinate_finishing_gc(MVMThreadContext *tc) {
    GCORCH_LOG("Coordinating GC termination...\n");
    
    /* We may have stolen the work of a blocked thread. Just decrement the count
     * of threads in GC for those ones now. */
    if (tc->stolen_for_gc) {
        MVMuint32 i = 0;
        while (tc->stolen_for_gc[i]) {
            GCORCH_LOG("Decrementing thread GC count for a bocked/stolen thread\n");
            apr_atomic_dec32(&tc->instance->starting_gc);
            i++;
        }
    }
    
    /* Now seek termination... */
    while (1) {
        MVMint8 termination_void = 0;
        
        /* While other threads are running GC, just look for if we or any of
         * our stolen threads get work in their in-tray. */
        while (tc->instance->starting_gc > 1) {
            /* Process our in-tray, and that of any stolen threads. */
            process_in_tray(tc);
            if (tc->stolen_for_gc) {
                MVMuint32 i = 0;
                while (tc->stolen_for_gc[i]) {
                    process_in_tray(tc->stolen_for_gc[i]);
                    i++;
                }
            }
        }
        
        /* We reached zero, but are we really done? Check all the in-trays. */
        /* XXX Need some care here...will this really be safe? */
        
        /* Check that we're still at zero. */
        if (tc->instance->starting_gc > 1)
            termination_void = 1;
            
        /* If termination wasn't voided, clean up after the run and do the
         * final decrement. */
        if (!termination_void) {
            GCORCH_LOG("Coordinator decided GC is terminated\n");
            cleanup_all(tc);
            apr_atomic_dec32(&tc->instance->starting_gc);
            break;
        }
    }
}

/* Called by a thread to indicate it is about to enter a blocking operation.
 * This gets any thread that is coordinating a GC run that this thread will
 * be unable to participate. */
void MVM_gc_mark_thread_blocked(MVMThreadContext *tc) {
    /* Try to set it from running to unable - the common case. */
    if (apr_atomic_cas32(&tc->gc_status, MVMGCStatus_UNABLE,
            MVMGCStatus_NONE) == MVMGCStatus_NONE)
        return;
    
    /* The only way this can fail is if another thread just decided we're to
     * participate in a GC run. */
    if (tc->gc_status == MVMGCStatus_INTERRUPT)
        MVM_gc_enter_from_interupt(tc);
    else
        MVM_panic(MVM_exitcode_gcorch, "Invalid GC status observed; aborting");
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

/* This is called when the allocator finds it has run out of memory and wants
 * to trigger a GC run. In this case, it's possible (probable, really) that it
 * will need to do that triggering, notifying other running threads that the
 * time has come to GC. */
void MVM_gc_enter_from_allocator(MVMThreadContext *tc) {
    MVMuint32  num_gc_threads;
    void      *limit;
    
    /* Grab the thread starting mutex while we start GC. This is so we
     * can get an accurate and stable number of threads that we expect to
     * join in with the GC. */
     if (apr_thread_mutex_lock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcorch, "Unable to lock user_threads mutex");
    num_gc_threads = tc->instance->num_user_threads + 1;
    
    /* Try to start the GC run. */
    if (apr_atomic_cas32(&tc->instance->expected_gc_threads, num_gc_threads, 0) == 0) {
        /* We are the winner of the GC starting race. This gives us some
         * extra responsibilities as well as doing the usual things.
         * First, increment GC sequence number. */
        GCORCH_LOG("GC thread elected coordinator\n");
        tc->instance->gc_seq_number++;

        /* Count us in to the GC run. */
        apr_atomic_inc32(&tc->instance->starting_gc);

        /* Ensure our stolen list is empty. */
        if (tc->stolen_for_gc) {
            free(tc->stolen_for_gc);
            tc->stolen_for_gc = NULL;
        }

        /* Signal other threads to do a GC run. */
        signal_all_but(tc);
        
        /* Now that we've signalled all threads we expect to join in,
         * we can safely release the thread starting mutex. */
        if (apr_thread_mutex_unlock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcorch, "Unable to unlock user_threads mutex");
        
        /* Wait for all thread to indicate readiness to collect. */
        wait_for_all_threads(tc->instance);
        
        /* Do GC work for this thread, or at least all we know about. */
        limit = tc->nursery_alloc;
        MVM_gc_collect(tc, MVMGCWhatToDo_All);
        
        /* Do GC work for any stolen threads. */
        if (tc->stolen_for_gc) {
            MVMuint32 i = 0;
            while (tc->stolen_for_gc[i]) {
                MVM_gc_collect(tc->stolen_for_gc[i], MVMGCWhatToDo_NoPerms);
                i++;
            }
        }

        /* Try to get everybody to agree we're done. */
        coordinate_finishing_gc(tc);
        
        /* Now we're all done, it's safe to finalize any objects that need it. */
        MVM_gc_collect_free_nursery_uncopied(tc, limit);
    }
    else {
        /* Another thread beat us to starting the GC sync process. Thus, act as
         * if we were interupted to GC; also release that thread starting mutex
         * that we (in the end needlessly) took. */
        if (apr_thread_mutex_unlock(tc->instance->mutex_user_threads) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcorch, "Unable to unlock user_threads mutex");
        MVM_gc_enter_from_interupt(tc);
    }
}

/* This is called when a thread hits an interupt at a GC safe point. This means
 * that another thread is already trying to start a GC run, so we don't need to
 * try and do that, just enlist in the run. */
void MVM_gc_enter_from_interupt(MVMThreadContext *tc) {
    void *limit;

    /* Count us in to the GC run. */
    GCORCH_LOG("Entered from interrupt\n");
    apr_atomic_inc32(&tc->instance->starting_gc);
    
    /* Ensure our stolen list is empty. */
    if (tc->stolen_for_gc) {
        free(tc->stolen_for_gc);
        tc->stolen_for_gc = NULL;
    }

    /* Wait for all thread to indicate readiness to collect. */
    wait_for_all_threads(tc->instance);
    
    /* Do GC work for this thread, or at least all we know about. */
    limit = tc->nursery_alloc;
    MVM_gc_collect(tc, MVMGCWhatToDo_NoPerms);

    /* Wait for completion, doing any extra work that we need to. */
    finish_gc(tc);

    /* Now we're all done, it's safe to finalize any objects that need it. */
    MVM_gc_collect_free_nursery_uncopied(tc, limit);
}
