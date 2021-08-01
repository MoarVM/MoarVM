#include "moar.h"

/* Combines a piece of work that will be passed to another thread with the
 * ID of the target thread to pass it to. */
typedef struct {
    MVMuint32        target;
    MVMGCPassedWork *work;
} ThreadWork;

/* Current chunks of work we're building up to pass. */
typedef struct {
    MVMuint32   num_target_threads;
    ThreadWork *target_work;
} WorkToPass;

/* Forward decls. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, WorkToPass *wtp, MVMuint8 gen);
static void pass_work_item(MVMThreadContext *tc, WorkToPass *wtp, MVMCollectable **item_ptr);
static void pass_leftover_work(MVMThreadContext *tc, WorkToPass *wtp);
static void add_in_tray_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);

/* The size of the nursery that a new thread should get. The main thread will
 * get a full-size one right away. */
MVMuint32 MVM_gc_new_thread_nursery_size(MVMInstance *i) {
    return i->main_thread != NULL
        ? (MVM_NURSERY_SIZE < MVM_NURSERY_THREAD_START
            ? MVM_NURSERY_SIZE
            : MVM_NURSERY_THREAD_START)
        : MVM_NURSERY_SIZE;
}

/* Does a garbage collection run. Exactly what it does is configured by the
 * couple of arguments that it takes.
 *
 * The what_to_do argument specifies where it should look for things to add
 * to the worklist: everywhere, just at thread local stuff, or just in the
 * thread's in-tray.
 *
 * The gen argument specifies whether to collect the nursery or both of the
 * generations. Nursery collection is done by semi-space copying. Once an
 * object is seen/copied once in the nursery (may be tuned in the future to
 * twice or so - we'll see) then it is not copied to tospace, but instead
 * promoted to the second generation. If we are collecting generation 2 also,
 * then objects that are alive in the second generation are simply marked.
 * Since the second generation is managed as a set of sized pools, there is
 * much less motivation for any kind of copying/compaction; the internal
 * fragmentation that makes finding a right-sized gap problematic will not
 * happen.
 *
 * Note that it adds the roots and processes them in phases, to try to avoid
 * building up a huge worklist. */
void MVM_gc_collect(MVMThreadContext *tc, MVMuint8 what_to_do, MVMuint8 gen) {
    /* Create a GC worklist. */
    MVMGCWorklist *worklist = MVM_gc_worklist_create(tc, gen != MVMGCGenerations_Nursery);

    /* Initialize work passing data structure. */
    WorkToPass wtp;
    wtp.num_target_threads = 0;
    wtp.target_work = NULL;

    /* See what we need to work on this time. */
    if (what_to_do == MVMGCWhatToDo_InTray) {
        /* We just need to process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from in tray \n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);
    }
    else if (what_to_do == MVMGCWhatToDo_Finalizing) {
        /* Need to process the finalizing queue. */
        MVMuint32 i;
        for (i = 0; i < tc->num_finalizing; i++)
            MVM_gc_worklist_add(tc, worklist, &(tc->finalizing[i]));
        GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from finalizing \n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);
    }
    else {
        /* Main collection run. The current tospace becomes fromspace, with
         * the size of the current tospace becoming stashed as the size of
         * that fromspace. */
        void *old_fromspace = tc->nursery_fromspace;
        MVMuint32 old_fromspace_size = tc->nursery_fromspace_size;
        tc->nursery_fromspace = tc->nursery_tospace;
        tc->nursery_fromspace_size = tc->nursery_tospace_size;

        /* Decide on this threads's tospace size. If fromspace was already at
         * the maximum nursery size, then that is the new tospace size. If
         * not, then see if this thread caused the current GC run, and grant
         * it a bigger tospace. Otherwise, new tospace size is left as the
         * last tospace size. */
        if (tc->nursery_tospace_size < MVM_NURSERY_SIZE) {
            if (tc->instance->thread_to_blame_for_gc == tc)
                tc->nursery_tospace_size *= 2;
        }

        /* If the old fromspace matches the target size, just re-use it. If
         * not, free it and allocate a new tospace. */
        if (old_fromspace_size == tc->nursery_tospace_size) {
            tc->nursery_tospace = old_fromspace;
        }
        else {
            MVM_free(old_fromspace);
            tc->nursery_tospace = MVM_calloc(1, tc->nursery_tospace_size);
        }

        /* Reset nursery allocation pointers to the new tospace. */
        tc->nursery_alloc       = tc->nursery_tospace;
        tc->nursery_alloc_limit = (char *)tc->nursery_tospace + tc->nursery_tospace_size;

        /* Add permanent roots and process them; only one thread will do
        * this, since they are instance-wide. */
        if (what_to_do != MVMGCWhatToDo_NoInstance) {
            MVM_gc_root_add_permanents_to_worklist(tc, worklist, NULL);
            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from instance permanents\n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
            MVM_gc_root_add_instance_roots_to_worklist(tc, worklist, NULL);
            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from instance roots\n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
        }

        /* Add per-thread state to worklist and process it. */
        MVM_gc_root_add_tc_roots_to_worklist(tc, worklist, NULL);
        GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from TC objects\n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* Add temporary roots and process them (these are per-thread). */
        MVM_gc_root_add_temps_to_worklist(tc, worklist, NULL);
        GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from thread temps\n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* Add things that are roots for the first generation because they are
        * pointed to by objects in the second generation and process them
        * (also per-thread). Note we need not do this if we're doing a full
        * collection anyway (in fact, we must not for correctness, otherwise
        * the gen2 rooting keeps them alive forever). */
        if (gen == MVMGCGenerations_Nursery) {
            MVM_gc_root_add_gen2s_to_worklist(tc, worklist);
            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from gen2 \n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
        }

        /* Process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : processing %d items from in tray \n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* At this point, we have probably done most of the work we will
         * need to (only get more if another thread passes us more); zero
         * out the remaining tospace. */
        memset(tc->nursery_alloc, 0, (char *)tc->nursery_alloc_limit - (char *)tc->nursery_alloc);
    }

    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);

    /* Pass any work for other threads we accumulated but that didn't trigger
     * the work passing threshold, then cleanup work passing list. */
    if (wtp.num_target_threads) {
        pass_leftover_work(tc, &wtp);
        MVM_free(wtp.target_work);
    }
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, WorkToPass *wtp, MVMuint8 gen) {
    MVMGen2Allocator  *gen2;
    MVMCollectable   **item_ptr;
    MVMCollectable    *new_addr;
    MVMuint32          gen2count;

    /* Grab the second generation allocator; we may move items into the
     * old generation. */
    gen2 = tc->gen2;

    while ((item_ptr = MVM_gc_worklist_get(tc, worklist))) {
        /* Dereference the object we're considering. */
        MVMCollectable *item = *item_ptr;
        MVMuint8 item_gen2;
        MVMuint8 to_gen2 = 0;

        /* If the item is NULL, that's fine - it's just a null reference and
         * thus we've no object to consider. */
        if (item == NULL)
            continue;

        /* If it's in the second generation and we're only doing a nursery,
         * collection, we have nothing to do. */
        item_gen2 = item->flags2 & MVM_CF_SECOND_GEN;
        if (item_gen2) {
            if (gen == MVMGCGenerations_Nursery)
                continue;
            if (item->flags2 & MVM_CF_GEN2_LIVE) {
                /* gen2 and marked as live. */
                continue;
            }
        } else if (item->flags2 & MVM_CF_FORWARDER_VALID) {
            /* If the item was already seen and copied, then it will have a
             * forwarding address already. Just update this pointer to the
             * new address and we're done. */
            if (MVM_GC_DEBUG_ENABLED(MVM_GC_DEBUG_COLLECT)) {
                if (*item_ptr != item->sc_forward_u.forwarder) {
                    GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : updating handle %p from %p to forwarder %p\n", item_ptr, item, item->sc_forward_u.forwarder);
                }
                else {
                    GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : already visited handle %p to forwarder %p\n", item_ptr, item->sc_forward_u.forwarder);
                }
            }
            MVM_barrier();
            *item_ptr = item->sc_forward_u.forwarder;
            continue;
        } else {
            /* If the pointer is already into tospace (the bit we've already
               copied into), we already updated it, so we're done. */
            if (item >= (MVMCollectable *)tc->nursery_tospace && item < (MVMCollectable *)tc->nursery_alloc) {
                continue;
            }
        }

        /* If it's owned by a different thread, we need to pass it over to
         * the owning thread. */
        if (item->owner != tc->thread_id) {
            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : sending a handle %p to object %p to thread %d\n", item_ptr, item, item->owner);
            pass_work_item(tc, wtp, item_ptr);
            continue;
        }

        /* If it's in to-space but *ahead* of our copy offset then it's an
           out-of-date pointer and we have some kind of corruption. */
        if (item >= (MVMCollectable *)tc->nursery_alloc && item < (MVMCollectable *)tc->nursery_alloc_limit)
            MVM_panic(1, "Heap corruption detected: pointer %p to past fromspace", item);

        /* At this point, we didn't already see the object, which means we
         * need to take some action. Go on the generation... */
        if (item_gen2) {
            assert(!(item->flags2 & MVM_CF_FORWARDER_VALID));
            /* It's in the second generation. We'll just mark it. */
            new_addr = item;
            if (MVM_GC_DEBUG_ENABLED(MVM_GC_DEBUG_COLLECT)) {
                GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : handle %p was already %p\n", item_ptr, new_addr);
            }
            item->flags2 |= MVM_CF_GEN2_LIVE;
            assert(*item_ptr == new_addr);
        } else {
            /* Catch NULL stable (always sign of trouble) in debug mode. */
            if (MVM_GC_DEBUG_ENABLED(MVM_GC_DEBUG_COLLECT) && !STABLE(item)) {
                GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : found a zeroed handle %p to object %p\n", item_ptr, item);
            }

            /* Did we see it in the nursery before, or should we move it to
             * gen2 anyway since either:
             *   * A persistent ID was requested?
             *   * It is referenced by a gen2 aggregate
             */
            if (item->flags1 & MVM_CF_HAS_OBJECT_ID
                || item->flags2 & (MVM_CF_NURSERY_SEEN | MVM_CF_REF_FROM_GEN2)) {
                /* Yes; we should move it to the second generation. Allocate
                 * space in the second generation. */
                to_gen2 = 1;
                new_addr = item->flags1 & MVM_CF_HAS_OBJECT_ID
                    ? MVM_gc_object_id_use_allocation(tc, item)
                    : MVM_gc_gen2_allocate(gen2, item->size);

                /* Add on to the promoted amount (used both to decide when to do
                 * the next full collection, as well as for profiling). Note we
                 * add unmanaged size on for objects below. */
                tc->gc_promoted_bytes += item->size;

                /* Copy the object to the second generation and mark it as
                 * living there. */
                GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : copying an object %p of size %d to gen2 %p\n",
                    item, item->size, new_addr);
                memcpy(new_addr, item, item->size);
                if (new_addr->flags2 & MVM_CF_NURSERY_SEEN)
                    new_addr->flags2 ^= MVM_CF_NURSERY_SEEN;
                new_addr->flags2 |= MVM_CF_SECOND_GEN;

                /* If it's a frame with an active work area, we need to keep
                 * on visiting it. Also add on object's unmanaged size. */
                if (new_addr->flags1 & MVM_CF_FRAME) {
                    if (((MVMFrame *)new_addr)->work)
                        MVM_gc_root_gen2_add(tc, (MVMCollectable *)new_addr);
                }
                else if (!(new_addr->flags1 & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE))) {
                    MVMObject *new_obj_addr = (MVMObject *)new_addr;
                    if (REPR(new_obj_addr)->unmanaged_size) {
                        MVMuint64 unmanaged_size =  REPR(new_obj_addr)->unmanaged_size(tc,
                            STABLE(new_obj_addr), OBJECT_BODY(new_obj_addr));
                        tc->gc_promoted_bytes += unmanaged_size;
                        if (tc->instance->profiling)
                            MVM_profiler_log_unmanaged_data_promoted(tc, unmanaged_size);
                    }
                }

                /* If we're going to sweep the second generation, also need
                 * to mark it as live. */
                if (gen == MVMGCGenerations_Both)
                    new_addr->flags2 |= MVM_CF_GEN2_LIVE;
            }
            else {
                /* No, so it will live in the nursery for another GC
                 * iteration. Allocate space in the nursery. */
                new_addr = (MVMCollectable *)tc->nursery_alloc;
                tc->nursery_alloc = (char *)tc->nursery_alloc + MVM_ALIGN_SIZE(item->size);
                GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : copying an object %p (reprid %d) of size %d to tospace %p\n",
                    item, (item->flags1 & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_FRAME)) ? -1 : (int)REPR(item)->ID, item->size, new_addr);

                /* Copy the object to tospace and mark it as seen in the
                 * nursery (so the next time around it will move to the
                 * older generation, if it survives). */
                memcpy(new_addr, item, item->size);
                new_addr->flags2 |= MVM_CF_NURSERY_SEEN;
            }

            /* Store the forwarding pointer and update the original
             * reference. */
            if (MVM_GC_DEBUG_ENABLED(MVM_GC_DEBUG_COLLECT) && new_addr != item) {
                GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : updating handle %p from referent %p (reprid %d) to %p\n", item_ptr, item, (item->flags1 & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_FRAME)) ? -1 : (int)REPR(item)->ID, new_addr);
            }
            *item_ptr = new_addr;
            item->sc_forward_u.forwarder = new_addr;
            MVM_barrier();
            /* Set the flag on the copy of item *in fromspace* to mark that the
               forwarder pointer is valid. */
            item->flags2 |= MVM_CF_FORWARDER_VALID;
        }

        /* Finally, we need to mark the collectable (at its moved address).
         * Track how many items we had before we mark it, in case we need
         * to write barrier them post-move to uphold the generational
         * invariant. */
        gen2count = worklist->items;
        MVM_gc_mark_collectable(tc, worklist, new_addr);

        /* In moving an object to generation 2, we may have left it pointing
         * to nursery objects. If so, make sure it's in the gen2 roots. */
        if (to_gen2) {
            MVMCollectable **j;
            MVMuint32 max = worklist->items, k;

            for (k = gen2count; k < max; k++) {
                j = worklist->list[k];
                if (*j)
                    MVM_gc_write_barrier_no_update_referenced(tc, new_addr, *j);
            }
        }
    }
}

/* Marks a collectable item (object, type object, STable). */
void MVM_gc_mark_collectable(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable *new_addr) {
    MVMuint16 i;
    MVMuint32 sc_idx;

    assert(!(new_addr->flags2 & MVM_CF_FORWARDER_VALID));
    /*assert(REPR(new_addr));*/
    sc_idx = MVM_sc_get_idx_of_sc(new_addr);
    if (sc_idx > 0) {
#if MVM_GC_DEBUG
        if (sc_idx >= tc->instance->all_scs_next_idx)
            MVM_panic(1, "SC index out of range");
#endif
        MVM_gc_worklist_add(tc, worklist, &(tc->instance->all_scs[sc_idx]->sc));
    }

    if (new_addr->flags1 & MVM_CF_TYPE_OBJECT) {
        /* Add the STable to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &((MVMObject *)new_addr)->st);
    }
    else if (new_addr->flags1 & MVM_CF_STABLE) {
        /* Add all references in the STable to the work list. */
        MVMSTable *new_addr_st = (MVMSTable *)new_addr;
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->method_cache);
        for (i = 0; i < new_addr_st->type_check_cache_length; i++)
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->type_check_cache[i]);
        if (new_addr_st->container_spec)
            if (new_addr_st->container_spec->gc_mark_data)
                new_addr_st->container_spec->gc_mark_data(tc, new_addr_st, worklist);
        if (new_addr_st->boolification_spec)
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->boolification_spec->method);
        if (new_addr_st->invocation_spec) {
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->class_handle);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->attr_name);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->invocation_handler);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->md_class_handle);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->md_cache_attr_name);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->md_valid_attr_name);
        }
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHO);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHAT);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->HOW);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->HOW_sc);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->method_cache_sc);
        if (new_addr_st->mode_flags & MVM_PARAMETRIC_TYPE) {
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->paramet.ric.parameterizer);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->paramet.ric.lookup);
        }
        else if (new_addr_st->mode_flags & MVM_PARAMETERIZED_TYPE) {
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->paramet.erized.parametric_type);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->paramet.erized.parameters);
        }

        /* If it needs to have its REPR data marked, do that. */
        if (new_addr_st->REPR->gc_mark_repr_data)
            new_addr_st->REPR->gc_mark_repr_data(tc, new_addr_st, worklist);
    }
    else if (new_addr->flags1 & MVM_CF_FRAME) {
        MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, (MVMFrame *)new_addr);
    }
    else {
        /* Need to view it as an object in here. */
        MVMObject *new_addr_obj = (MVMObject *)new_addr;

        /* Add the STable to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &new_addr_obj->st);

        /* If needed, mark it. This will add addresses to the worklist
         * that will need updating. Note that we are passing the address
         * of the object *after* copying it since those are the addresses
         * we care about updating; the old chunk of memory is now dead! */
        if (MVM_GC_DEBUG_ENABLED(MVM_GC_DEBUG_COLLECT) && !STABLE(new_addr_obj))
            MVM_panic(MVM_exitcode_gcnursery, "Found an outdated reference to address %p", new_addr);
        if (REPR(new_addr_obj)->gc_mark)
            REPR(new_addr_obj)->gc_mark(tc, STABLE(new_addr_obj), OBJECT_BODY(new_addr_obj), worklist);
    }
}

/* Adds a chunk of work to another thread's in-tray. */
static void push_work_to_thread_in_tray(MVMThreadContext *tc, MVMuint32 target, MVMGCPassedWork *work) {
    MVMGCPassedWork * volatile *target_tray;

    /* Locate the thread to pass the work to. */
    MVMThreadContext *target_tc = NULL;
    if (target == 1) {
        /* It's going to the main thread. */
        target_tc = tc->instance->main_thread;
    }
    else {
        MVMThread *t = (MVMThread *)MVM_load(&tc->instance->threads);
        do {
            if (t->body.tc && t->body.tc->thread_id == target) {
                target_tc = t->body.tc;
                break;
            }
        } while ((t = t->body.next));
        if (!target_tc)
            MVM_panic(MVM_exitcode_gcnursery, "Internal error: invalid thread ID %d in GC work pass", target);
    }

    /* Pass the work, chaining any other in-tray entries for the thread
     * after us. */
    target_tray = &target_tc->gc_in_tray;
    while (1) {
        MVMGCPassedWork *orig = *target_tray;
        work->next = orig;
        if (MVM_casptr(target_tray, orig, work) == orig)
            return;
    }
}

/* Adds work to list of items to pass over to another thread, and if we
 * reach the pass threshold then does the passing. */
static void pass_work_item(MVMThreadContext *tc, WorkToPass *wtp, MVMCollectable **item_ptr) {
    ThreadWork *target_info = NULL;
    MVMuint32   target      = (*item_ptr)->owner;
    MVMuint32   j;

    /* Find any existing thread work passing list for the target. */
    if (target == 0)
        MVM_panic(MVM_exitcode_gcnursery, "Internal error: zeroed target thread ID in work pass");
    for (j = 0; j < wtp->num_target_threads; j++) {
        if (wtp->target_work[j].target == target) {
            target_info = &wtp->target_work[j];
            break;
        }
    }

    /* If there's no entry for this target, create one. */
    if (target_info == NULL) {
        wtp->num_target_threads++;
        wtp->target_work = MVM_realloc(wtp->target_work,
            wtp->num_target_threads * sizeof(ThreadWork));
        target_info = &wtp->target_work[wtp->num_target_threads - 1];
        target_info->target = target;
        target_info->work   = NULL;
    }

    /* See if there's a currently active list; create it if not. */
    if (!target_info->work) {
        target_info->work = MVM_calloc(1, sizeof(MVMGCPassedWork));
    }

    /* Add this item to the work list. */
    target_info->work->items[target_info->work->num_items] = item_ptr;
    target_info->work->num_items++;

    /* If we've hit the limit, pass this work to the target thread. */
    if (target_info->work->num_items == MVM_GC_PASS_WORK_SIZE) {
        push_work_to_thread_in_tray(tc, target, target_info->work);
        target_info->work = NULL;
    }
}

/* Passes all work for other threads that we've got left in our to-pass list. */
static void pass_leftover_work(MVMThreadContext *tc, WorkToPass *wtp) {
    MVMuint32 j;
    for (j = 0; j < wtp->num_target_threads; j++)
        if (wtp->target_work[j].work)
            push_work_to_thread_in_tray(tc, wtp->target_work[j].target,
                wtp->target_work[j].work);
}

/* Takes work in a thread's in-tray, if any, and adds it to the worklist. */
static void add_in_tray_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMGCPassedWork * volatile *in_tray = &tc->gc_in_tray;
    MVMGCPassedWork *head;

    /* Get work to process. */
    while (1) {
        /* See if there's anything in the in-tray; if not, we're done. */
        head = *in_tray;
        if (head == NULL)
            return;

        /* Otherwise, try to take it. */
        if (MVM_casptr(in_tray, head, NULL) == head)
            break;
    }

    /* Go through list, adding to worklist. */
    while (head) {
        MVMGCPassedWork *next = head->next;
        MVMuint32 i;
        for (i = 0; i < head->num_items; i++)
            MVM_gc_worklist_add(tc, worklist, head->items[i]);
        MVM_free(head);
        head = next;
    }
}

/* Save dead STable pointers to delete later.. */
static void MVM_gc_collect_enqueue_stable_for_deletion(MVMThreadContext *tc, MVMSTable *st) {
    MVMSTable *old_head;
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
    assert(!(st->header.flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED));
#endif
    do {
        old_head = tc->instance->stables_to_free;
        st->header.sc_forward_u.st = old_head;
    } while (!MVM_trycas(&tc->instance->stables_to_free, old_head, st));
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_collect_free_nursery_uncopied(MVMThreadContext *executing_thread, MVMThreadContext *tc, void *limit) {
    /* We start scanning the fromspace, and keep going until we hit
     * the end of the area allocated in it. */
    void *scan = tc->nursery_fromspace;

    MVMuint8 do_prof_log = 0;

    if (executing_thread->prof_data)
        do_prof_log = 1;

    while (scan < limit) {
        /* The object here is dead if it never got a forwarding pointer
         * written in to it. */
        MVMCollectable *item = (MVMCollectable *)scan;
        MVMuint8 dead = !(item->flags2 & MVM_CF_FORWARDER_VALID);

        if (!dead)
            assert(item->sc_forward_u.forwarder != NULL);

        /* Now go by collectable type. */
        if (item->flags1 & MVM_CF_TYPE_OBJECT) {
            /* Type object */
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
            if (dead && item->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
                MVM_free(item->sc_forward_u.sci);
#endif
            if (dead && item->flags1 & MVM_CF_HAS_OBJECT_ID)
                MVM_gc_object_id_clear(tc, item);
        }
        else if (item->flags1 & MVM_CF_STABLE) {
            MVMSTable *st = (MVMSTable *)item;
            if (dead) {
/*            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : enqueuing an STable %d in the nursery to be freed\n", item);*/
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                if (item->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED) {
                    MVM_free(item->sc_forward_u.sci);
                    /* Arguably we don't need to do this, if we're always
                       consistent about what we put on the stable queue. */
                    item->flags1 &= ~MVM_CF_SERIALZATION_INDEX_ALLOCATED;
                }
#endif
                MVM_gc_collect_enqueue_stable_for_deletion(tc, st);
            }
        }
        else if (item->flags1 & MVM_CF_FRAME) {
            if (dead)
                MVM_frame_destroy(tc, (MVMFrame *)item);
        }
        else {
            /* Object instance. If dead, call gc_free if needed. Scan is
             * incremented by object size. */
            MVMObject *obj = (MVMObject *)item;
            GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : collecting an object %p in the nursery with reprid %d\n", item, REPR(obj)->ID);
            if (dead && REPR(obj)->gc_free)
                REPR(obj)->gc_free(tc, obj);
            if (dead && do_prof_log) {
                MVM_profiler_log_gc_deallocate(executing_thread, obj);
            }
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
            if (dead && item->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
                MVM_free(item->sc_forward_u.sci);
#endif
            if (dead && item->flags1 & MVM_CF_HAS_OBJECT_ID)
                MVM_gc_object_id_clear(tc, item);
        }

        /* Go to the next item. */
        scan = (char *)scan + MVM_ALIGN_SIZE(item->size);
    }
}

/* Free STables (in any thread/generation!) queued to be freed. */
void MVM_gc_collect_free_stables(MVMThreadContext *tc) {
#if MVM_GC_DEBUG < 3
    MVMSTable *st = tc->instance->stables_to_free;
    while (st) {
        MVMSTable *st_to_free = st;
        st = st_to_free->header.sc_forward_u.st;
        st_to_free->header.sc_forward_u.st = NULL;
        MVM_6model_stable_gc_free(tc, st_to_free);
    }
#endif
    tc->instance->stables_to_free = NULL;
}

/* Goes through the unmarked objects in the second generation heap and builds
 * free lists out of them. Also does any required finalization. */
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *executing_thread, MVMThreadContext *tc, MVMint32 global_destruction) {
    /* Visit each of the size class bins. */
    MVMGen2Allocator *gen2 = tc->gen2;
    MVMuint32 bin, obj_size, page, i;
    MVMuint8 do_prof_log = 0;

    char ***freelist_insert_pos;

    if (executing_thread->prof_data)
        do_prof_log = 1;

    for (bin = 0; bin < MVM_GEN2_BINS; bin++) {
        /* If we've nothing allocated in this size class, skip it. */
        if (gen2->size_classes[bin].pages == NULL)
            continue;

        /* Calculate object size for this bin. */
        obj_size = (bin + 1) << MVM_GEN2_BIN_BITS;

        /* freelist_insert_pos is a pointer to a memory location that
         * stores the address of the last traversed free list node (char **). */
        /* Initialize freelist insertion position to free list head. */
        freelist_insert_pos = &gen2->size_classes[bin].free_list;

        /* Visit each page. */
        for (page = 0; page < gen2->size_classes[bin].num_pages; page++) {
            /* Visit all the objects, looking for dead ones and reset the
             * mark for each of them. */
            char *cur_ptr = gen2->size_classes[bin].pages[page];
            char *end_ptr = page + 1 == gen2->size_classes[bin].num_pages
                ? gen2->size_classes[bin].alloc_pos
                : cur_ptr + obj_size * MVM_GEN2_PAGE_ITEMS;
            while (cur_ptr < end_ptr) {
                MVMCollectable *col = (MVMCollectable *)cur_ptr;

                /* Is this already a free list slot? If so, it becomes the
                 * new free list insert position. */
                if (*freelist_insert_pos == (char **)cur_ptr) {
                    freelist_insert_pos = (char ***)cur_ptr;
                }

                /* Otherwise, it must be a collectable of some kind. Is it
                 * live? */
                else if (col->flags2 & MVM_CF_GEN2_LIVE) {
                    /* Yes; clear the mark. */
                    col->flags2 &= ~MVM_CF_GEN2_LIVE;
                }
                else {
                    GCDEBUG_LOG(tc, MVM_GC_DEBUG_COLLECT, "Thread %d run %d : collecting an object %p in the gen2\n", col);
                    /* No, it's dead. Do any cleanup. */
#if MVM_GC_DEBUG
                    col->flags2 |= MVM_CF_DEBUG_IN_GEN2_FREE_LIST;
#endif
                    if (col->flags1 & MVM_CF_TYPE_OBJECT) {
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                        if (col->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
                            MVM_free(col->sc_forward_u.sci);
#endif
                    }
                    else if (col->flags1 & MVM_CF_STABLE) {
                        if (
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                            !(col->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED) &&
#endif
                            col->sc_forward_u.sc.sc_idx == 0
                            && col->sc_forward_u.sc.idx == (unsigned)MVM_DIRECT_SC_IDX_SENTINEL) {
                            /* We marked it dead last time, kill it. */
                            MVM_6model_stable_gc_free(tc, (MVMSTable *)col);
                        }
                        else {
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                            if (col->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED) {
                                /* Whatever happens next, we can free this
                                   memory immediately, because no-one will be
                                   serializing a dead STable. */
                                assert(!(col->sc_forward_u.sci->sc_idx == 0
                                         && col->sc_forward_u.sci->idx
                                         == MVM_DIRECT_SC_IDX_SENTINEL));
                                MVM_free(col->sc_forward_u.sci);
                                col->flags1 &= ~MVM_CF_SERIALZATION_INDEX_ALLOCATED;
                            }
#endif
                            if (global_destruction) {
                                /* We're in global destruction, so enqueue to the end
                                 * like we do in the nursery */
                                MVM_gc_collect_enqueue_stable_for_deletion(tc, (MVMSTable *)col);
                            } else {
                                /* There will definitely be another gc run, so mark it as "died last time". */
                                col->sc_forward_u.sc.sc_idx = 0;
                                col->sc_forward_u.sc.idx = MVM_DIRECT_SC_IDX_SENTINEL;
                            }
                            /* Skip the freelist updating. */
                            cur_ptr += obj_size;
                            continue;
                        }
                    }
                    else if (col->flags1 & MVM_CF_FRAME) {
                        MVM_frame_destroy(tc, (MVMFrame *)col);
                    }
                    else {
                        /* Object instance; call gc_free if needed. */
                        MVMObject *obj = (MVMObject *)col;
                        if (do_prof_log) {
                            MVM_profiler_log_gc_deallocate(executing_thread, obj);
                        }
                        if (STABLE(obj) && REPR(obj)->gc_free)
                            REPR(obj)->gc_free(tc, obj);
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                        if (col->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
                            MVM_free(col->sc_forward_u.sci);
#endif
                    }

                    /* Chain in to the free list. */
                    *((char **)cur_ptr) = (char *)*freelist_insert_pos;
                    *freelist_insert_pos = (char **)cur_ptr;

                    /* Update the pointer to the insert position to point to us */
                    freelist_insert_pos = (char ***)cur_ptr;
                }

                /* Move to the next object. */
                cur_ptr += obj_size;
            }
        }
    }
    
    /* Also need to consider overflows. */
    for (i = 0; i < gen2->num_overflows; i++) {
        if (gen2->overflows[i]) {
            MVMCollectable *col = gen2->overflows[i];
            if (col->flags2 & MVM_CF_GEN2_LIVE) {
                /* A living over-sized object; just clear the mark. */
                col->flags2 &= ~MVM_CF_GEN2_LIVE;
            }
            else {
                /* Dead over-sized object. We know if it's this big it cannot
                 * be a type object or STable, so only need handle the simple
                 * object case. */
                if (!(col->flags1 & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_FRAME))) {
                    MVMObject *obj = (MVMObject *)col;
                    if (REPR(obj)->gc_free)
                        REPR(obj)->gc_free(tc, obj);
#ifdef MVM_USE_OVERFLOW_SERIALIZATION_INDEX
                    if (col->flags1 & MVM_CF_SERIALZATION_INDEX_ALLOCATED)
                        MVM_free(col->sc_forward_u.sci);
#endif
                }
                else {
                    MVM_panic(MVM_exitcode_gcnursery, "Internal error: gen2 overflow contains non-object");
                }
                MVM_free(col);
                gen2->overflows[i] = NULL;
            }
        }
    }
    /* And finally compact the overflow list */
    MVM_gc_gen2_compact_overflows(gen2);
}
