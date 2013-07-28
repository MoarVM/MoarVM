#include "moarvm.h"

#define GCORCH_DEBUG 0
#ifdef _MSC_VER
# define GCORCH_LOG(tc, msg, ...) if (GCORCH_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number, __VA_ARGS__)
#else
# define GCORCH_LOG(tc, msg, ...) if (GCORCH_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number , ##__VA_ARGS__)
#endif

/* If we have the job of doing GC for a thread, we add it to our work
 * list. */
static void add_work(MVMThreadContext *tc, MVMThreadContext *stolen) {
    if (tc->gc_work == NULL) {
        tc->gc_work_size = 16;
        tc->gc_work = malloc(tc->gc_work_size * sizeof(MVMWorkThread));
    }
    else if (tc->gc_work_count == tc->gc_work_size) {
        tc->gc_work_size *= 2;
        tc->gc_work = realloc(tc->gc_work, tc->gc_work_size * sizeof(MVMWorkThread));
    }
    tc->gc_work[tc->gc_work_count++].tc = stolen;
}

/* Goes through all threads but the current one and notifies them that a
 * GC run is starting. Those that are blocked are considered excluded from
 * the run, and are not counted. Returns the count of threads that should be
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
                    GCORCH_LOG(tc, "Thread %d run %d : Signalled thread %d to interrupt\n", to_signal->thread_id);
                    return 1;
                }
                break;
            case MVMGCStatus_INTERRUPT:
                GCORCH_LOG(tc, "Thread %d run %d : thread %d already interrupted\n", to_signal->thread_id);
                return 0;
            case MVMGCStatus_UNABLE:
                /* Otherwise, it's blocked; try to set it to work Stolen. */
                if (apr_atomic_cas32(&to_signal->gc_status, MVMGCStatus_STOLEN,
                        MVMGCStatus_UNABLE) == MVMGCStatus_UNABLE) {
                    GCORCH_LOG(tc, "Thread %d run %d : A blocked thread %d spotted; work stolen\n", to_signal->thread_id);
                    add_work(tc, to_signal);
                    return 0;
                }
                break;
            /* this case occurs if a child thread is Stolen by its parent
             * before we get to it in the chain. */
            case MVMGCStatus_STOLEN:
                GCORCH_LOG(tc, "Thread %d run %d : thread %d already stolen (it was a spawning child)\n", to_signal->thread_id);
                return 0;
            default:
                MVM_panic(MVM_exitcode_gcorch, "invalid status %d in GC orchestrate\n", to_signal->gc_status);
                return 0;
        }
    }
}
static MVMuint32 signal_all_but(MVMThreadContext *tc, MVMThread *t, MVMThread *tail) {
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
                    count += signal_one_thread(tc, t->body.tc);
                }
                break;
            case MVM_thread_stage_exited:
                GCORCH_LOG(tc, "Thread %d run %d : queueing to clear nursery of thread %d\n", t->body.tc->thread_id);
                add_work(tc, t->body.tc);
                break;
            case MVM_thread_stage_clearing_nursery:
                GCORCH_LOG(tc, "Thread %d run %d : queueing to destroy thread %d\n", t->body.tc->thread_id);
                /* last GC run for this thread */
                add_work(tc, t->body.tc);
                break;
            case MVM_thread_stage_destroyed:
                GCORCH_LOG(tc, "Thread %d run %d : found a destroyed thread\n");
                /* will be cleaned up (removed from the lists) shortly */
                break;
            default:
                MVM_panic(MVM_exitcode_gcorch, "Corrupted MVMThread or running threads list: invalid thread stage %d", t->body.stage);
        }
    } while (next && (t = next));
    if (tail)
        MVM_WB(tc, t, tail);
    t->body.next = tail;
    return count;
}

/* Does work in a thread's in-tray, if any. */
static void process_in_tray(MVMThreadContext *tc, MVMuint8 gen, MVMuint32 *put_vote) {
    /* Do we have any more work given by another thread? If so, re-enter
     * GC loop to process it. Note that since we're now doing GC stuff
     * again, we take back our vote to finish. */
    if (tc->gc_in_tray) {
        GCORCH_LOG(tc, "Thread %d run %d : Was given extra work by another thread; doing it\n");
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
    while ((work = next)) {
        next = work->last_by_sender;
        free(work);
    }
    tc->gc_sent_items = NULL;
}

/* Called by a thread when it thinks it is done with GC. It may get some more
 * work yet, though. */
static void finish_gc(MVMThreadContext *tc, MVMuint8 gen) {
    MVMuint32 put_vote = 1, i;

    /* Loop until other threads have terminated, processing any extra work
     * that we are given. */
    while (tc->instance->gc_finish) {
        MVMuint32 failed = 0;
        MVMuint32 i = 0;

        for ( ; i < tc->gc_work_count; i++) {
            process_in_tray(tc->gc_work[i].tc, gen, &put_vote);
            failed = process_sent_items(tc->gc_work[i].tc, &put_vote) | failed;
        }

        if (!failed && put_vote) {
            MVM_atomic_decr(&tc->instance->gc_finish);
            put_vote = 0;
        }
    }
/*    GCORCH_LOG(tc, "Thread %d run %d : Discovered GC termination\n");*/

    /* Reset GC status flags and cleanup sent items for any work threads. */
    /* This is also where thread destruction happens, and it needs to happen
     * before we acknowledge this GC run is finished. */
    for (i = 0; i < tc->gc_work_count; i++) {
        MVMThreadContext *other = tc->gc_work[i].tc;
        MVMThread *thread_obj = other->thread_obj;
        cleanup_sent_items(other);
        if (thread_obj->body.stage == MVM_thread_stage_clearing_nursery) {
            GCORCH_LOG(tc, "Thread %d run %d : freeing gen2 of thread %d\n", other->thread_id);
            /* always free gen2 */
            MVM_gc_collect_free_gen2_unmarked(other);
            GCORCH_LOG(tc, "Thread %d run %d : transferring gen2 of thread %d\n", other->thread_id);
            MVM_gc_gen2_transfer(other, tc);
            GCORCH_LOG(tc, "Thread %d run %d : destroying thread %d\n", other->thread_id);
            MVM_tc_destroy(other);
            tc->gc_work[i].tc = thread_obj->body.tc = NULL;
            thread_obj->body.stage = MVM_thread_stage_destroyed;
        }
        else {
            if (thread_obj->body.stage == MVM_thread_stage_exited) {
                /* don't bother freeing gen2; we'll do it next time */
                thread_obj->body.stage = MVM_thread_stage_clearing_nursery;
//                    GCORCH_LOG(tc, "Thread %d run %d : set thread %d clearing nursery stage to %d\n", other->thread_id, thread_obj->body.stage);
            }
            apr_atomic_cas32(&other->gc_status, MVMGCStatus_UNABLE,
                MVMGCStatus_STOLEN);
            apr_atomic_cas32(&other->gc_status, MVMGCStatus_NONE,
                MVMGCStatus_INTERRUPT);
        }
    }
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

static void signal_child(MVMThreadContext *tc) {
    MVMThread *child = tc->thread_obj->body.new_child;
    /* if we still have it, its state will be UNABLE, so steal it. */
    if (child) {
        /* this will never return nonzero, because the child's status
         * will always be UNABLE or STOLEN. */
        signal_one_thread(tc, child->body.tc);
        while (!MVM_cas(&tc->thread_obj->body.new_child,
            tc->thread_obj->body.new_child, NULL));
    }
}

static void run_gc(MVMThreadContext *tc, MVMuint8 what_to_do) {
    MVMuint8   gen;
    MVMThread *child;
    MVMuint32  i, n;

    /* Do GC work for this thread, or at least all we know about. */
    gen = tc->instance->gc_seq_number % MVM_GC_GEN2_RATIO == 0
        ? MVMGCGenerations_Both
        : MVMGCGenerations_Nursery;

    /* Do GC work for any work threads. */
    for (i = 0, n = tc->gc_work_count ; i < n; i++) {
        MVMThreadContext *other = tc->gc_work[i].tc;
        tc->gc_work[i].limit = other->nursery_alloc;
        GCORCH_LOG(tc, "Thread %d run %d : starting collection for thread %d\n",
            other->thread_id);
        MVM_gc_collect(other, (other == tc ? what_to_do : MVMGCWhatToDo_NoInstance), gen);
    }

    /* Wait for everybody to agree we're done. */
    finish_gc(tc, gen);

    /* Now we're all done, it's safe to finalize any objects that need it. */
    for (i = 0, n = tc->gc_work_count ; i < n; i++) {
        MVMThreadContext *other = tc->gc_work[i].tc;
        MVMThread *thread_obj;

        /* the thread might've been destroyed */
        if (!other) continue;

        thread_obj = other->thread_obj;

        MVM_gc_collect_free_nursery_uncopied(other, tc->gc_work[i].limit);

        if (gen == MVMGCGenerations_Both) {
            GCORCH_LOG(tc, "Thread %d run %d : freeing gen2 of thread %d\n", other->thread_id);
            MVM_gc_collect_cleanup_gen2roots(other);
            MVM_gc_collect_free_gen2_unmarked(other);
        }
    }
}

/* This is called when the allocator finds it has run out of memory and wants
 * to trigger a GC run. In this case, it's possible (probable, really) that it
 * will need to do that triggering, notifying other running threads that the
 * time has come to GC. */
void MVM_gc_enter_from_allocator(MVMThreadContext *tc) {

    GCORCH_LOG(tc, "Thread %d run %d : Entered from allocate\n");

    /* Try to start the GC run. */
    if (MVM_cas(&tc->instance->gc_start, 0, 1)) {
        MVMThread *last_starter = NULL;
        MVMuint32 num_threads = 0;

        /* We are the winner of the GC starting race. This gives us some
         * extra responsibilities as well as doing the usual things.
         * First, increment GC sequence number. */
        tc->instance->gc_seq_number++;
        GCORCH_LOG(tc, "Thread %d run %d : GC thread elected coordinator: starting gc seq %d\n", tc->instance->gc_seq_number);

        /* Ensure our stolen list is empty. */
        tc->gc_work_count = 0;

        /* need to wait for other threads to reset their gc_status. */
        while (tc->instance->gc_ack)
            apr_thread_yield();

        add_work(tc, tc);

        /* grab our child */
        signal_child(tc);

        do {
            if (tc->instance->threads && tc->instance->threads != last_starter) {
                MVMThread *head;
                MVMuint32 add;
                while (!MVM_cas(&tc->instance->threads, (head = tc->instance->threads), NULL));

                add = signal_all_but(tc, head, last_starter);
                last_starter = head;
                if (add) {
                    GCORCH_LOG(tc, "Thread %d run %d : Found %d other threads\n", add);
                    MVM_atomic_add(&tc->instance->gc_start, add);
                    num_threads += add;
                }
            }
        } while (tc->instance->gc_start > 1);

        if (!MVM_cas(&tc->instance->threads, NULL, last_starter))
            MVM_panic(MVM_exitcode_gcorch, "threads list corrupted\n");

        if (tc->instance->gc_finish != 0)
            MVM_panic(MVM_exitcode_gcorch, "finish votes was %d\n", tc->instance->gc_finish);

        tc->instance->gc_ack = tc->instance->gc_finish = num_threads + 1;
        GCORCH_LOG(tc, "Thread %d run %d : finish votes is %d\n", (int)tc->instance->gc_finish);

        /* signal to the rest to start */
        if (MVM_atomic_decr(&tc->instance->gc_start) != 1)
            MVM_panic(MVM_exitcode_gcorch, "start votes was %d\n", tc->instance->gc_finish);

        run_gc(tc, MVMGCWhatToDo_All);
    }
    else {
        /* Another thread beat us to starting the GC sync process. Thus, act as
         * if we were interrupted to GC. */
        GCORCH_LOG(tc, "Thread %d run %d : Lost coordinator election\n");
        MVM_gc_enter_from_interrupt(tc);
    }
}

/* This is called when a thread hits an interrupt at a GC safe point. This means
 * that another thread is already trying to start a GC run, so we don't need to
 * try and do that, just enlist in the run. */
void MVM_gc_enter_from_interrupt(MVMThreadContext *tc) {
    MVMuint8 decr = 0;
    AO_t curr;

    tc->gc_work_count = 0;

    add_work(tc, tc);

    /* grab our child */
    signal_child(tc);

    /* Count us in to the GC run. Wait for a vote to steal. */
    GCORCH_LOG(tc, "Thread %d run %d : Entered from interrupt\n");

    while ((curr = tc->instance->gc_start) < 2
            || !MVM_cas(&tc->instance->gc_start, curr, curr - 1)) {
    /*    apr_sleep(1);
        apr_thread_yield();*/
    }

    /* Wait for all threads to indicate readiness to collect. */
    while (tc->instance->gc_start) {
    /*    apr_sleep(1);
        apr_thread_yield();*/
    }
    run_gc(tc, MVMGCWhatToDo_NoInstance);
}
