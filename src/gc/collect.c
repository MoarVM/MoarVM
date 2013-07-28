#include "moarvm.h"

#define GCCOLL_DEBUG 0
#ifdef _MSC_VER
# define GCCOLL_LOG(tc, msg, ...) if (GCCOLL_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number, __VA_ARGS__)
#else
# define GCCOLL_LOG(tc, msg, ...) if (GCCOLL_DEBUG) printf((msg), (tc)->thread_id, (tc)->instance->gc_seq_number , ##__VA_ARGS__)
#endif

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

/* Foward decls. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, WorkToPass *wtp, MVMuint8 gen);
static void pass_work_item(MVMThreadContext *tc, WorkToPass *wtp, MVMCollectable **item_ptr);
static void pass_leftover_work(MVMThreadContext *tc, WorkToPass *wtp);
static void add_in_tray_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist);

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
    MVMGCWorklist *worklist = MVM_gc_worklist_create(tc);

    /* Initialize work passing data structure. */
    WorkToPass wtp;
    wtp.num_target_threads = 0;
    wtp.target_work = NULL;

    /* If we're starting a run (as opposed to just coming back here to do a
     * little more work we got after we first thought we were done...) */
    if (what_to_do != MVMGCWhatToDo_InTray) {
        /* Swap fromspace and tospace. */
        void * fromspace = tc->nursery_tospace;
        void * tospace   = tc->nursery_fromspace;
        tc->nursery_fromspace = fromspace;
        tc->nursery_tospace   = tospace;

        /* Reset nursery allocation pointers to the new tospace. */
        tc->nursery_alloc       = tospace;
        tc->nursery_alloc_limit = (char *)tc->nursery_alloc + MVM_NURSERY_SIZE;

        MVM_gc_worklist_add(tc, worklist, &tc->thread_obj);
        GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from thread_obj\n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* Add permanent roots and process them; only one thread will do
        * this, since they are instance-wide. */
        if (what_to_do != MVMGCWhatToDo_NoInstance) {
            MVM_gc_root_add_permanents_to_worklist(tc, worklist);
            GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from instance permanents\n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
            MVM_gc_root_add_instance_roots_to_worklist(tc, worklist);
            GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from instance roots\n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
        }

        /* Add per-thread state to worklist and process it. */
        MVM_gc_root_add_tc_roots_to_worklist(tc, worklist);
        GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from TC objects\n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* Add temporary roots and process them (these are per-thread). */
        MVM_gc_root_add_temps_to_worklist(tc, worklist);
        GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from thread temps\n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* Add things that are roots for the first generation because the are
        * pointed to by objects in the second generation and process them
        * (also per-thread). Note we need not do this if we're doing a full
        * collection anyway (in fact, we must not for correctness, otherwise
        * the gen2 rooting keeps them alive forever). */
        if (gen == MVMGCGenerations_Nursery) {
            MVM_gc_root_add_gen2s_to_worklist(tc, worklist);
            GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from gen2 \n", worklist->items);
            process_worklist(tc, worklist, &wtp, gen);
        }

        /* Find roots in frames and process them. */
        if (tc->cur_frame) {
            MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, tc->cur_frame);
            GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from cur_frame \n", worklist->items);
			process_worklist(tc, worklist, &wtp, gen);
		}

        /* Process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from in tray \n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);

        /* At this point, we have probably done most of the work we will
         * need to (only get more if another thread passes us more); zero
         * out the remaining tospace. */
        memset(tc->nursery_alloc, 0, (char *)tc->nursery_alloc_limit - (char *)tc->nursery_alloc);
    }
    else {
        /* We just need to process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        GCCOLL_LOG(tc, "Thread %d run %d : processing %d items from in tray \n", worklist->items);
        process_worklist(tc, worklist, &wtp, gen);
    }

    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);

    /* Pass any work for other threads we accumulated but that didn't trigger
     * the work passing threshold, then cleanup work passing list. */
    if (wtp.num_target_threads) {
        pass_leftover_work(tc, &wtp);
        free(wtp.target_work);
    }

    /* If it was a full collection, some of the things in gen2 that we root
     * due to point to gen1 objects may be dead. */
    if (gen != MVMGCGenerations_Nursery)
        MVM_gc_root_gen2_cleanup(tc);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, WorkToPass *wtp, MVMuint8 gen) {
    MVMGen2Allocator  *gen2;
    MVMCollectable   **item_ptr;
    MVMCollectable    *new_addr;
    MVMuint32          size, gen2count;
    MVMuint16          i;

    /* Grab the second generation allocator; we may move items into the
     * old generation. */
    gen2 = tc->gen2;

    while ((item_ptr = MVM_gc_worklist_get(tc, worklist))) {
        /* Dereference the object we're considering. */
        MVMCollectable *item = *item_ptr;
        MVMuint8 item_gen2;

        /* If the item is NULL, that's fine - it's just a null reference and
         * thus we've no object to consider. */
        if (item == NULL)
            continue;

        /* If it's in the second generation and we're only doing a nursery,
         * collection, we have nothing to do. */
        item_gen2 = item->flags & MVM_CF_SECOND_GEN;
        if (item_gen2 && gen == MVMGCGenerations_Nursery)
            continue;

        /* If the item was already seen and copied, then it will have a
         * forwarding address already. Just update this pointer to the
         * new address and we're done. */
        if (item->forwarder) {
            if (GCCOLL_DEBUG) {
                if (*item_ptr != item->forwarder) {
                    GCCOLL_LOG(tc, "Thread %d run %d : updating handle %p from %p to forwarder %p\n", item_ptr, item, item->forwarder);
                }
                else {
                    GCCOLL_LOG(tc, "Thread %d run %d : already visited handle %p to forwarder %p\n", item_ptr, item->forwarder);
                }
            }
            *item_ptr = item->forwarder;
            continue;
        }

        /* If the pointer is already into tospace, we already updated it,
         * so we're done. */
        if (item >= (MVMCollectable *)tc->nursery_tospace && item < (MVMCollectable *)tc->nursery_alloc_limit)
            continue;

        /* If it's owned by a different thread, we need to pass it over to
         * the owning thread. */
        if (item->owner != tc->thread_id) {
            GCCOLL_LOG(tc, "Thread %d run %d : sending a handle %p to object %p to thread %d\n", item_ptr, item, item->owner);
            pass_work_item(tc, wtp, item_ptr);
            continue;
        }

        /* At this point, we didn't already see the object, which means we
         * need to take some action. Go on the generation... */
        if (item_gen2) {
            /* It's in the second generation. We'll just mark it, which is
             * done by setting the forwarding pointer to the object itself,
             * since we don't do moving. */
            new_addr = item;
            if (GCCOLL_DEBUG) {
                if (new_addr != item) {
                    GCCOLL_LOG(tc, "Thread %d run %d : updating handle %p from referent %p to %p\n", item_ptr, item, new_addr);
                }
                else {
                    GCCOLL_LOG(tc, "Thread %d run %d : handle %p was already %p\n", item_ptr, new_addr);
                }
            }
            *item_ptr = item->forwarder = new_addr;
        } else {
            if (GCCOLL_DEBUG && !STABLE(item)) {
                GCCOLL_LOG(tc, "Thread %d run %d : found a zeroed handle %p to object %p\n", item_ptr, item);
                printf("%d", ((MVMCollectable *)1)->owner);
            }
            /* We've got a live object in the nursery; this means some kind of
             * copying is going to happen. Work out the size. */
            if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE)))
                size = ((MVMObject *)item)->st->size;
            else if (item->flags & MVM_CF_TYPE_OBJECT)
                size = sizeof(MVMObject);
            else if (item->flags & MVM_CF_STABLE)
                size = sizeof(MVMSTable);
            else
                MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC sizing");

            /* Did we see it in the nursery before? */
            if (item->flags & MVM_CF_NURSERY_SEEN) {
                /* Yes; we should move it to the second generation. Allocate
                 * space in the second generation. */
                new_addr = MVM_gc_gen2_allocate(gen2, size);

                /* Copy the object to the second generation and mark it as
                 * living there. */
                GCCOLL_LOG(tc, "Thread %d run %d : copying an object %p of size %d to gen2 %p\n", item, size, new_addr);
                memcpy(new_addr, item, size);
                new_addr->flags ^= MVM_CF_NURSERY_SEEN;
                new_addr->flags |= MVM_CF_SECOND_GEN;

                /* If it references frames or static frames, we need to keep
                 * on visiting it. */
                if (!(new_addr->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE))) {
                    MVMObject *new_obj_addr = (MVMObject *)new_addr;
                    if (REPR(new_obj_addr)->refs_frames)
                        MVM_gc_root_gen2_add(tc, (MVMCollectable *)new_obj_addr);
                }

                /* If we're going to sweep the second generation, also need
                 * to mark it as live. */
                if (gen == MVMGCGenerations_Both)
                    new_addr->forwarder = new_addr;
            }
            else {
                /* No, so it will live in the nursery for another GC
                 * iteration. Allocate space in the nursery. */
                new_addr = (MVMCollectable *)tc->nursery_alloc;
                tc->nursery_alloc = (char *)tc->nursery_alloc + size;
                GCCOLL_LOG(tc, "Thread %d run %d : copying an object %p of size %d to tospace %p\n", item, size, new_addr);

                /* Copy the object to tospace and mark it as seen in the
                 * nursery (so the next time around it will move to the
                 * older generation, if it survives). */
                memcpy(new_addr, item, size);
                new_addr->flags |= MVM_CF_NURSERY_SEEN;
            }

            /* Store the forwarding pointer and update the original
             * reference. */
            if (GCCOLL_DEBUG && new_addr != item) {
                GCCOLL_LOG(tc, "Thread %d run %d : updating handle %p from referent %p to %p\n", item_ptr, item, new_addr);
            }
            *item_ptr = item->forwarder = new_addr;
        }

        /* Finally, we need to mark the collectable (at its moved address).
         * Track how many items we had before we mark it, in case we need
         * to write barrier them post-move to uphold the generational
         * invariant. */
        gen2count = worklist->items;
        MVM_gc_mark_collectable(tc, worklist, new_addr);

        /* In moving an object to generation 2, we may have left it pointing
         * to nursery objects. If so, make sure it's in the gen2 roots. */
        if (new_addr->flags & MVM_CF_SECOND_GEN) {
            MVMCollectable **j;
            MVMuint32 max = worklist->items, k;

            for (k = gen2count; k < max; k++) {
                j = worklist->list[k];
                if (*j)
                    MVM_WB(tc, new_addr, *j);
            }
        }
    }
}

/* Marks a collectable item (object, type object, STable). */
void MVM_gc_mark_collectable(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMCollectable *new_addr) {
    MVMuint16 i;

    MVM_gc_worklist_add(tc, worklist, &new_addr->sc);

    if (!(new_addr->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE))) {
        /* Need to view it as an object in here. */
        MVMObject *new_addr_obj = (MVMObject *)new_addr;

        /* Add the STable to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &new_addr_obj->st);

        /* If needed, mark it. This will add addresses to the worklist
         * that will need updating. Note that we are passing the address
         * of the object *after* copying it since those are the addresses
         * we care about updating; the old chunk of memory is now dead! */
        if (GCCOLL_DEBUG && !STABLE(new_addr_obj))
            MVM_panic(MVM_exitcode_gcnursery, "Found an outdated reference to address %p", new_addr);
        if (REPR(new_addr_obj)->gc_mark)
            REPR(new_addr_obj)->gc_mark(tc, STABLE(new_addr_obj), OBJECT_BODY(new_addr_obj), worklist);
    }
    else if (new_addr->flags & MVM_CF_TYPE_OBJECT) {
        /* Add the STable to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &((MVMObject *)new_addr)->st);
    }
    else if (new_addr->flags & MVM_CF_STABLE) {
        /* Add all references in the STable to the work list. */
        MVMSTable *new_addr_st = (MVMSTable *)new_addr;
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->HOW);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHAT);
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->method_cache);
        for (i = 0; i < new_addr_st->vtable_length; i++)
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->vtable[i]);
        for (i = 0; i < new_addr_st->type_check_cache_length; i++)
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->type_check_cache[i]);
        if (new_addr_st->container_spec) {
            new_addr_st->container_spec->gc_mark_data(tc, new_addr_st, worklist);
        }
        if (new_addr_st->boolification_spec)
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->boolification_spec->method);
        if (new_addr_st->invocation_spec) {
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->class_handle);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->attr_name);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->invocation_spec->invocation_handler);
        }
        MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHO);

        /* If it needs to have its REPR data marked, do that. */
        if (new_addr_st->REPR->gc_mark_repr_data)
            new_addr_st->REPR->gc_mark_repr_data(tc, new_addr_st, worklist);
    }
    else {
        MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC marking");
    }
}

/* Adds a chunk of work to another thread's in-tray. */
static void push_work_to_thread_in_tray(MVMThreadContext *tc, MVMuint32 target, MVMGCPassedWork *work) {
    MVMint32 j;
    MVMGCPassedWork * volatile *target_tray;

    /* Locate the thread to pass the work to. */
    MVMThreadContext *target_tc = NULL;
    if (target == 0) {
        /* It's going to the main thread. */
        target_tc = tc->instance->main_thread;
    }
    else {
        MVMThread *t = tc->instance->threads;
        do {
            if (t->body.tc->thread_id == target) {
                target_tc = t->body.tc;
                break;
            }
        } while ((t = t->body.next));
        if (!target_tc)
            MVM_panic(MVM_exitcode_gcnursery, "Internal error: invalid thread ID in GC work pass");
    }

    /* push to sent_items list */
    if (tc->gc_sent_items) {
        tc->gc_sent_items->next_by_sender = work;
        work->last_by_sender = tc->gc_sent_items;
    }
    /* queue it up to check if the check list isn't clear */
    if (!tc->gc_next_to_check) {
        tc->gc_next_to_check = work;
    }
    tc->gc_sent_items = work;

    /* Pass the work, chaining any other in-tray entries for the thread
     * after us. */
    target_tray = &target_tc->gc_in_tray;
    while (1) {
        MVMGCPassedWork *orig = *target_tray;
        work->next = orig;
        if (apr_atomic_casptr((volatile void **)target_tray, work, orig) == orig)
            return;
    }
}

/* Adds work to list of items to pass over to another thread, and if we
 * reach the pass threshold then does the passing. */
static void pass_work_item(MVMThreadContext *tc, WorkToPass *wtp, MVMCollectable **item_ptr) {
    ThreadWork *target_info = NULL;
    MVMuint32   target      = (*item_ptr)->owner;
    MVMuint32   j;
    MVMInstance *i          = tc->instance;

    /* Find any existing thread work passing list for the target. */
    for (j = 0; j < wtp->num_target_threads; j++) {
        if (wtp->target_work[j].target == target) {
            target_info = &wtp->target_work[j];
            break;
        }
    }

    /* If there's no entry for this target, create one. */
    if (target_info == NULL) {
        wtp->num_target_threads++;
        wtp->target_work = realloc(wtp->target_work,
            wtp->num_target_threads * sizeof(ThreadWork));
        target_info = &wtp->target_work[wtp->num_target_threads - 1];
        target_info->target = target;
        target_info->work   = NULL;
    }

    /* See if there's a currently active list; create it if not. */
    if (!target_info->work) {
        target_info->work = calloc(sizeof(MVMGCPassedWork), 1);
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
        if (apr_atomic_casptr((volatile void **)in_tray, NULL, head) == head)
            break;
    }

    /* Go through list, adding to worklist. */
    while (head) {
        MVMGCPassedWork *next = head->next;
        MVMuint32 i;
        for (i = 0; i < head->num_items; i++)
            MVM_gc_worklist_add(tc, worklist, head->items[i]);
        head->completed = 1;
        head = next;
    }
}

/* Some objects, having been copied, need no further attention. Others
 * need to do some additional freeing, however. This goes through the
 * fromspace and does any needed work to free uncopied things (this may
 * run in parallel with the mutator, which will be operating on tospace). */
void MVM_gc_collect_free_nursery_uncopied(MVMThreadContext *tc, void *limit) {
    /* We start scanning the fromspace, and keep going until we hit
     * the end of the area allocated in it. */
    void *scan = tc->nursery_fromspace;
    while (scan < limit) {
        /* The object here is dead if it never got a forwarding pointer
         * written in to it. */
        MVMCollectable *item = (MVMCollectable *)scan;
        MVMuint8 dead = item->forwarder == NULL;

        /* Now go by collectable type. */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE))) {
            /* Object instance. If dead, call gc_free if needed. Scan is
             * incremented by object size. */
            MVMObject *obj = (MVMObject *)item;
/*            GCCOLL_LOG(tc, "Thread %d run %d : collecting an object %d in the nursery\n", item);*/
            if (dead && REPR(obj)->gc_free)
                REPR(obj)->gc_free(tc, obj);
            scan = (char *)scan + STABLE(obj)->size;
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            /* Type object; doesn't have anything extra that needs freeing. */
            scan = (char *)scan + sizeof(MVMObject);
        }
        else if (item->flags & MVM_CF_STABLE) {
            /* Dead STables are a little interesting. Of course, there is
             * stuff to free, but there's also an ordering issue: we need
             * to make sure we don't toss these until we freed up all the
             * other things, since the size data held in the STable may be
             * needed in order to finish walking the fromspace! So we will
             * add them to a list and then free them all at the end. */
            if (dead) {
                MVM_panic(MVM_exitcode_gcnursery, "Can't free STables in the GC yet");
            }
            scan = (char *)scan + sizeof(MVMSTable);
        }
        else {
            printf("item flags: %d\n", item->flags);
            MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC free");
        }
    }
}

/* Goes through the inter-generational roots and removes any that have been
* determined dead. Should run just after gen2 GC has run but before building
* the free list (which clears the marks). */
void MVM_gc_collect_cleanup_gen2roots(MVMThreadContext *tc) {
    MVMCollectable **gen2roots = tc->gen2roots;
    MVMuint32        num_roots = tc->num_gen2roots;
    MVMuint32        ins_pos   = 0;
    MVMuint32        i;
    for (i = 0; i < num_roots; i++)
        if (gen2roots[i]->forwarder)
            gen2roots[ins_pos++] = gen2roots[i];
    tc->num_gen2roots = ins_pos;
}

/* Goes through the unmarked objects in the second generation heap and builds
 * free lists out of them. Also does any required finalization. */
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *tc) {
    /* Visit each of the size class bins. */
    MVMGen2Allocator *gen2 = tc->gen2;
    MVMuint32 bin, obj_size, page;
    char ***freelist_insert_pos;
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
            char **last_insert_pos = NULL;
            while (cur_ptr < end_ptr) {
                MVMCollectable *col = (MVMCollectable *)cur_ptr;

                /* Is this already a free list slot? If so, it becomes the
                 * new free list insert position. */
                if (*freelist_insert_pos == (char **)cur_ptr) {
                    freelist_insert_pos = (char ***)cur_ptr;
                }

                /* Otherwise, it must be a collectable of some kind. Is it
                 * live? */
                else if (col->forwarder) {
                    /* Yes; clear the mark. */
                    col->forwarder = NULL;
                }
                else {
                    GCCOLL_LOG(tc, "Thread %d run %d : collecting an object %p in the gen2\n", col);
                    /* No, it's dead. Do any cleanup. */
                    if (!(col->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE))) {
                        /* Object instance; call gc_free if needed. */
                        MVMObject *obj = (MVMObject *)col;
                        if (REPR(obj)->gc_free)
                            REPR(obj)->gc_free(tc, obj);
                    }
                    else if (col->flags & MVM_CF_TYPE_OBJECT) {
                        /* Type object; doesn't have anything extra that needs freeing. */
                    }
                    else if (col->flags & MVM_CF_STABLE) {
                        MVM_panic(MVM_exitcode_gcnursery, "Can't free STables in gen2 GC yet");
                    }
                    else {
                        printf("item flags: %d\n", col->flags);
                        MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in gen2 GC free");
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
}
