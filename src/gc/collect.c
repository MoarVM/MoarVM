#include "moarvm.h"

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
static void pass_work_item(MVMInstance *i, WorkToPass *wtp, MVMCollectable **item_ptr);
static void pass_leftover_work(MVMInstance *i, WorkToPass *wtp);
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

        /* Add permanent roots and process them; only one thread will do
        * this, since they are instance-wide. */
        if (what_to_do != MVMGCWhatToDo_NoInstance) {
            MVM_gc_root_add_permanents_to_worklist(tc, worklist);
            process_worklist(tc, worklist, &wtp, gen);
            MVM_gc_root_add_instance_roots_to_worklist(tc, worklist);
            process_worklist(tc, worklist, &wtp, gen);
        }

        /* Add temporary roots and process them (these are per-thread). */
        MVM_gc_root_add_temps_to_worklist(tc, worklist);
        process_worklist(tc, worklist, &wtp, gen);
        
        /* Add things that are roots for the first generation because
        * they are pointed to by objects in the second generation and
        * process them (also per-thread). */
        MVM_gc_root_add_gen2s_to_worklist(tc, worklist);
        process_worklist(tc, worklist, &wtp, gen);
        
        /* Find roots in frames and process them. */
        if (tc->cur_frame)
            MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, tc->cur_frame);
        
        /* Process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        process_worklist(tc, worklist, &wtp, gen);

        /* At this point, we have probably done most of the work we will
         * need to (only get more if another thread passes us more); zero
         * out the remaining tospace. */
        memset(tc->nursery_alloc, 0, (char *)tc->nursery_alloc_limit - (char *)tc->nursery_alloc);
    }
    else {
        /* We just need to process anything in the in-tray. */
        add_in_tray_to_worklist(tc, worklist);
        process_worklist(tc, worklist, &wtp, gen);
    }

    /* Destroy the worklist. */
    MVM_gc_worklist_destroy(tc, worklist);
    
    /* Pass any work for other threads we accumulated but that didn't trigger
     * the work passing threshold, then cleanup work passing list. */
    if (wtp.num_target_threads) {
        pass_leftover_work(tc->instance, &wtp);
        free(wtp.target_work);
    }
    
    /* At this point, some of the objects in the generation 2 worklist may
     * have been promoted into generation 2 itself, in which case they no
     * longer need to reside in the worklist. */
    MVM_gc_root_gen2_cleanup_promoted(tc);
}

/* Processes the current worklist. */
static void process_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, WorkToPass *wtp, MVMuint8 gen) {
    MVMGen2Allocator  *gen2;
    MVMCollectable   **item_ptr;
    MVMCollectable    *new_addr;
    MVMuint32          size;
    MVMuint16          i;
    
    /* Grab the second generation allocator; we may move items into the
     * old generation. */
    gen2 = tc->gen2;
    
    while (item_ptr = MVM_gc_worklist_get(tc, worklist)) {
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
            pass_work_item(tc->instance, wtp, item_ptr);
            continue;
        }
        
        /* At this point, we didn't already see the object, which means we
         * need to take some action. Go on the generation... */
        if (item_gen2) {
            /* It's in the second generation. We'll just mark it, which is
             * done by setting the forwarding pointer to the object itself,
             * since we don't do moving. */
             new_addr = item;
             *item_ptr = item->forwarder = new_addr;
        } else {
            /* We've got a live object in the nursery; this means some kind of
            * copying is going to happen. Work out the size. */
            if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC)))
                size = ((MVMObject *)item)->st->size;
            else if (item->flags & MVM_CF_TYPE_OBJECT)
                size = sizeof(MVMObject);
            else if (item->flags & MVM_CF_STABLE)
                size = sizeof(MVMSTable);
            else if (item->flags & MVM_CF_SC)
                MVM_panic(MVM_exitcode_gcnursery, "Can't handle serialization contexts in the GC yet");
            else
                MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC sizing");
            
            /* Did we see it in the nursery before? */
            if (item->flags & MVM_CF_NURSERY_SEEN) {
                /* Yes; we should move it to the second generation. Allocate
                * space in the second generation. */
                new_addr = MVM_gc_gen2_allocate(gen2, size);
                
                /* Copy the object to the second generation and mark it as
                * living there. */
                memcpy(new_addr, item, size);
                new_addr->flags ^= MVM_CF_NURSERY_SEEN;
                new_addr->flags |= MVM_CF_SECOND_GEN;
            }
            else {
                /* No, so it will live in the nursery for another GC
                * iteration. Allocate space in the nursery. */
                new_addr = (MVMCollectable *)tc->nursery_alloc;
                tc->nursery_alloc = (char *)tc->nursery_alloc + size;
                
                /* Copy the object to tospace and mark it as seen in the
                * nursery (so the next time around it will move to the
                * older generation, if it survives). */
                memcpy(new_addr, item, size);
                new_addr->flags |= MVM_CF_NURSERY_SEEN;
            }
            
            /* Store the forwarding pointer and update the original
            * reference. */
            *item_ptr = item->forwarder = new_addr;
        }

        /* Add the serialization context address to the worklist. */
        MVM_gc_worklist_add(tc, worklist, &new_addr->sc);
        
        /* Otherwise, we need to do the copy. What sort of thing are we
         * going to copy? */
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* Need to view it as an object in here. */
            MVMObject *new_addr_obj = (MVMObject *)new_addr;
            
            /* Add the STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &new_addr_obj->st);
            
            /* If needed, mark it. This will add addresses to the worklist
             * that will need updating. Note that we are passing the address
             * of the object *after* copying it since those are the addresses
             * we care about updating; the old chunk of memory is now dead! */
            if (REPR(new_addr_obj)->gc_mark)
                REPR(new_addr_obj)->gc_mark(tc, STABLE(new_addr_obj), OBJECT_BODY(new_addr_obj), worklist);
        }
        else if (item->flags & MVM_CF_TYPE_OBJECT) {
            /* Add the STable to the worklist. */
            MVM_gc_worklist_add(tc, worklist, &((MVMObject *)new_addr)->st);
        }
        else if (item->flags & MVM_CF_STABLE) {
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
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->value_slot.class_handle);
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->value_slot.attr_name);
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->container_spec->fetch_method);
            }
            if (new_addr_st->boolification_spec)
                MVM_gc_worklist_add(tc, worklist, &new_addr_st->boolification_spec->method);
            MVM_gc_worklist_add(tc, worklist, &new_addr_st->WHO);
            
            /* If it needs to have its REPR data marked, do that. */
            if (new_addr_st->REPR->gc_mark_repr_data)
                new_addr_st->REPR->gc_mark_repr_data(tc, new_addr_st, worklist);
        }
        else if (item->flags & MVM_CF_SC) {
            /* Add all references in the SC to the work list. */
            MVMSerializationContext *new_addr_sc = (MVMSerializationContext *)new_addr;
            MVMint64 i;
            
            MVM_gc_worklist_add(tc, worklist, &new_addr_sc->handle);
            MVM_gc_worklist_add(tc, worklist, &new_addr_sc->description);
            MVM_gc_worklist_add(tc, worklist, &new_addr_sc->root_objects);
            MVM_gc_worklist_add(tc, worklist, &new_addr_sc->root_codes);

            for (i = 0; i < new_addr_sc->num_stables; i++)
                MVM_gc_worklist_add(tc, worklist, &new_addr_sc->root_stables[i]);
        }
        else {
            MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC marking");
        }
    }
}

/* Adds a chunk of work to another thread's in-tray. */
static void push_work_to_thread_in_tray(MVMInstance *i, MVMuint32 target, MVMGCPassedWork *work) {
    MVMint32 j;
    
    /* Locate the thread to pass the work to. */
    MVMThreadContext *target_tc = NULL;
    if (target == 0) {
        /* It's going to the main thread. */
        target_tc = i->main_thread;
    }
    else {
        for (j = 0; j < i->num_user_threads; j++) {
            MVMThread *t = i->user_threads[j];
            if (t->body.tc->thread_id == target) {
                target_tc = t->body.tc;
                break;
            }
        }
    }
    if (!target_tc)
        MVM_panic(MVM_exitcode_gcnursery, "Internal error: invalid thread ID in GC work pass");

    /* Pass the work, chaining any other in-tray entries for the thread
     * after us. */
    while (1) {
        MVMGCPassedWork *orig = target_tc->gc_in_tray;
        work->next = orig;
        if (apr_atomic_casptr(&target_tc->gc_in_tray, work, orig) == orig)
            return;
    }
}

/* Adds work to list of items to pass over to another thread, and if we 
 * reach the pass threshold then doing the passing. */
static void pass_work_item(MVMInstance *i, WorkToPass *wtp, MVMCollectable **item_ptr) {
    ThreadWork *target_info = NULL;
    MVMuint32   target      = (*item_ptr)->owner;
    MVMuint32   j;
    
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
        target_info->work = malloc(sizeof(MVMGCPassedWork));
        target_info->work->num_items = 0;
        target_info->work->next = NULL;
    }

    /* Add this item to the work list. */
    target_info->work->items[target_info->work->num_items] = item_ptr;
    target_info->work->num_items++;

    /* If we've hit the limit, pass this work to the target thread. */
    if (target_info->work->num_items == MVM_GC_PASS_WORK_SIZE) {
        push_work_to_thread_in_tray(i, target, target_info->work);
        target_info->work = NULL;
    }
}

/* Passes all work for other threads that we've got left in our to-pass list. */
static void pass_leftover_work(MVMInstance *i, WorkToPass *wtp) {
    MVMuint32 j;
    for (j = 0; j < wtp->num_target_threads; j++)
        if (wtp->target_work[j].work)
            push_work_to_thread_in_tray(i, wtp->target_work[j].target,
                wtp->target_work[j].work);
}

/* Takes work in a thread's in-tray, if any, and adds it to the worklist. */
static void add_in_tray_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMGCPassedWork *head;
    
    /* Get work to process. */
    while (1) {
        /* See if there's anything in the in-tray; if not, we're done. */
        head = tc->gc_in_tray;
        if (head == NULL)
            return;

        /* Otherwise, try to take it. */
        if (apr_atomic_casptr(&tc->gc_in_tray, NULL, head) == head)
            break;
    }

    /* Go through list, adding to worklist. */
    while (head) {
        MVMGCPassedWork *next = head->next;
        MVMuint32 i;
        for (i = 0; i < head->num_items; i++)
            MVM_gc_worklist_add(tc, worklist, head->items[i]);
        free(head);
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
        if (!(item->flags & (MVM_CF_TYPE_OBJECT | MVM_CF_STABLE | MVM_CF_SC))) {
            /* Object instance. If dead, call gc_free if needed. Scan is
             * incremented by object size. */
            MVMObject *obj = (MVMObject *)item;
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
        else if (item->flags & MVM_CF_SC) {
            MVM_panic(MVM_exitcode_gcnursery, "Can't free serialization contexts in the GC yet");
        }
        else {
            printf("item flags: %d\n", item->flags);
            MVM_panic(MVM_exitcode_gcnursery, "Internal error: impossible case encountered in GC free");
        }
    }
}

/* Goes through the unmarked objects in the second generation heap and builds free
 * lists out of them. Also does any required finalization. */
void MVM_gc_collect_free_gen2_unmarked(MVMThreadContext *tc) {
    /* XXX TODO. */
}
