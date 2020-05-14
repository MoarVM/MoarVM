#include "moar.h"

/* Adds a location holding a collectable object to the permanent list of GC
 * roots, so that it will always be marked and never die. Note that the
 * address of the collectable must be passed, since it will need to be
 * updated. */
void MVM_gc_root_add_permanent_desc(MVMThreadContext *tc, MVMCollectable **obj_ref, char *description) {
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as a permanent root");

    uv_mutex_lock(&tc->instance->mutex_permroots);
    /* Allocate extra permanent root space if needed. */
    if (tc->instance->num_permroots == tc->instance->alloc_permroots) {
        tc->instance->alloc_permroots *= 2;
        tc->instance->permroots = MVM_realloc(tc->instance->permroots,
            sizeof(MVMCollectable **) * tc->instance->alloc_permroots);
        tc->instance->permroot_descriptions = MVM_realloc(
            tc->instance->permroot_descriptions,
            sizeof(char *) * tc->instance->alloc_permroots);
    }

    /* Add this one to the list. */
    tc->instance->permroots[tc->instance->num_permroots] = obj_ref;
    tc->instance->permroot_descriptions[tc->instance->num_permroots] = description;
    tc->instance->num_permroots++;

    uv_mutex_unlock(&tc->instance->mutex_permroots);
}

void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    MVM_gc_root_add_permanent_desc(tc, obj_ref, "<\?\?>");
}

/* Adds the set of permanently registered roots to a GC worklist. */
void MVM_gc_root_add_permanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMuint32         i, num_roots;
    MVMCollectable ***permroots;
    num_roots = tc->instance->num_permroots;
    permroots = tc->instance->permroots;
    if (worklist) {
        for (i = 0; i < num_roots; i++)
            MVM_gc_worklist_add(tc, worklist, permroots[i]);
    }
    else {
        char **permroot_descriptions = tc->instance->permroot_descriptions;
        for (i = 0; i < num_roots; i++)
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot,
                *(permroots[i]), permroot_descriptions[i]);
    }
}

/* This macro factors out the logic to check if we're adding to a GC worklist
 * or a heap snapshot, and does the appropriate thing. */
#define add_collectable(tc, worklist, snapshot, col, desc) \
    do { \
        if (worklist) { \
            MVM_gc_worklist_add(tc, worklist, &(col)); \
        } \
        else { \
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, \
                (MVMCollectable *)col, desc); \
        } \
    } while (0)

/* Adds anything that is a root thanks to being referenced by instance,
 * but that isn't permanent. */
void MVM_gc_root_add_instance_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMString                  **int_to_str_cache;
    MVMuint32                    i;

    add_collectable(tc, worklist, snapshot, tc->instance->threads, "Thread list");
    add_collectable(tc, worklist, snapshot, tc->instance->compiler_registry, "Compiler registry");
    add_collectable(tc, worklist, snapshot, tc->instance->hll_syms, "HLL symbols");
    add_collectable(tc, worklist, snapshot, tc->instance->clargs, "Command line args");

    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_thread,
        "Event loop thread");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_todo_queue,
        "Event loop todo queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_permit_queue,
        "Event loop permit queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_cancel_queue,
        "Event loop cancel queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_active,
        "Event loop active task list");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_free_indices,
        "Event loop active free indices list");

    add_collectable(tc, worklist, snapshot, tc->instance->spesh_thread,
        "Specialization thread");
    add_collectable(tc, worklist, snapshot, tc->instance->spesh_queue,
        "Specialization log queue");

    if (worklist)
        MVM_spesh_plan_gc_mark(tc, tc->instance->spesh_plan, worklist);

    int_to_str_cache = tc->instance->int_to_str_cache;
    for (i = 0; i < MVM_INT_TO_STR_CACHE_SIZE; i++)
        add_collectable(tc, worklist, snapshot, int_to_str_cache[i],
            "Integer to string cache entry");

    /* okay, so this makes the weak hash slightly less weak.. for certain
     * keys of it anyway... */
    MVMStrHashTable *const weakhash = &tc->instance->sc_weakhash;
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, weakhash);
    while (!MVM_str_hash_at_end(tc, weakhash, iterator)) {
        struct MVMSerializationContextWeakHashEntry *current
            = MVM_str_hash_current_nocheck(tc, weakhash, iterator);
        /* mark the string handle pointer iff it hasn't yet been resolved */
        add_collectable(tc, worklist, snapshot, current->hash_handle.key,
            "SC weakhash hash key");
        if (!current->scb->sc)
            add_collectable(tc, worklist, snapshot, current->scb->handle,
                "SC weakhash unresolved handle");
        else if (!current->scb->claimed)
            add_collectable(tc, worklist, snapshot, current->scb->sc,
                "SC weakhash unclaimed SC");
        iterator = MVM_str_hash_next_nocheck(tc, weakhash, iterator);
    }

    MVMStrHashTable *const containers = &tc->instance->container_registry;
    iterator = MVM_str_hash_first(tc, containers);
    while (!MVM_str_hash_at_end(tc, containers, iterator)) {
        MVMContainerRegistry *registry = MVM_str_hash_current_nocheck(tc, containers, iterator);
        add_collectable(tc, worklist, snapshot, registry->hash_handle.key,
                        "Container configuration hash key");
        iterator = MVM_str_hash_next_nocheck(tc, containers, iterator);
    }

    add_collectable(tc, worklist, snapshot, tc->instance->cached_backend_config,
        "Cached backend configuration hash");

    add_collectable(tc, worklist, snapshot, tc->instance->env_hash,
        "Cached environment variable hash");

    add_collectable(tc, worklist, snapshot, tc->instance->sig_arr,
        "Cached signal mapping array");

    if (tc->instance->confprog)
        MVM_confprog_mark(tc, worklist, snapshot);

    add_collectable(tc, worklist, snapshot, tc->instance->subscriptions.subscription_queue,
        "VM Event Subscription Queue");

    add_collectable(tc, worklist, snapshot, tc->instance->subscriptions.GCEvent,
        "VM Event GCEvent type");
    add_collectable(tc, worklist, snapshot, tc->instance->subscriptions.GCEvent,
        "VM Event SpeshOverviewEvent type");

    MVM_disp_registry_mark(tc, worklist);
    MVM_debugserver_mark_handles(tc, worklist, snapshot);
}

/* Adds anything that is a root thanks to being referenced by a thread,
 * context, but that isn't permanent. */
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    /* The call stack. */
    MVM_callstack_mark(tc, worklist, snapshot);

    /* Any active exception handlers and payload. */
    MVMActiveHandler *cur_ah = tc->active_handlers;
    while (cur_ah != NULL) {
        add_collectable(tc, worklist, snapshot, cur_ah->ex_obj, "Active handler exception object");
        if (!MVM_FRAME_IS_ON_CALLSTACK(tc, cur_ah->frame))
            add_collectable(tc, worklist, snapshot, cur_ah->frame, "Active handler frame");
        cur_ah = cur_ah->next_handler;
    }
    add_collectable(tc, worklist, snapshot, tc->last_payload,
        "Last exception payload");

    /* The thread object. */
    add_collectable(tc, worklist, snapshot, tc->thread_obj, "Thread object");

    /* The thread's entry frame. */
    if (tc->thread_entry_frame && !MVM_FRAME_IS_ON_CALLSTACK(tc, tc->thread_entry_frame))
        add_collectable(tc, worklist, snapshot, tc->thread_entry_frame, "Thread entry frame");

    /* Any exception handler result. */
    add_collectable(tc, worklist, snapshot, tc->last_handler_result, "Last handler result");

    /* List of SCs currently being compiled. */
    add_collectable(tc, worklist, snapshot, tc->compiling_scs, "Compiling serialization contexts");

    /* compunit variable pointer (and be null if thread finished) */
    if (tc->interp_cu)
        add_collectable(tc, worklist, snapshot, *(tc->interp_cu), "Current interpreter compilation unit");
    /* Current dispatcher. */
    add_collectable(tc, worklist, snapshot, tc->cur_dispatcher, "Current dispatcher");
    add_collectable(tc, worklist, snapshot, tc->cur_dispatcher_for, "Current dispatcher for");
    add_collectable(tc, worklist, snapshot, tc->next_dispatcher, "Next dispatcher");
    add_collectable(tc, worklist, snapshot, tc->next_dispatcher_for, "Next dispatcher for");

    /* Callback cache. */
    MVMStrHashTable *cache = &tc->native_callback_cache;
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, cache);
    while (!MVM_str_hash_at_end(tc, cache, iterator)) {
        struct MVMNativeCallbackCacheHead *current_cbceh
            = MVM_str_hash_current_nocheck(tc, cache, iterator);
        MVMint32 i;
        MVMNativeCallback *entry = current_cbceh->head;
        add_collectable(tc, worklist, snapshot, current_cbceh->hash_handle.key,
                        "Native callback cache key");
        while (entry) {
            for (i = 0; i < entry->num_types; i++)
                add_collectable(tc, worklist, snapshot, entry->types[i],
                                "Native callback cache type");
            add_collectable(tc, worklist, snapshot, entry->target,
                            "Native callback cache target");
            entry = entry->next;
        }
        iterator = MVM_str_hash_next_nocheck(tc, cache, iterator);
    }

    /* Profiling data. */
    if (worklist)
        MVM_profile_instrumented_mark_data(tc, worklist);

    /* Specialization log, stack simulation, and plugin state. */
    add_collectable(tc, worklist, snapshot, tc->spesh_log, "Specialization log");
    if (worklist)
        MVM_spesh_sim_stack_gc_mark(tc, tc->spesh_sim_stack, worklist);
    else
        MVM_spesh_sim_stack_gc_describe(tc, snapshot, tc->spesh_sim_stack);
    if (tc->spesh_active_graph) {
        if (worklist)
            MVM_spesh_graph_mark(tc, tc->spesh_active_graph, worklist);
        else
            MVM_spesh_graph_describe(tc, tc->spesh_active_graph, snapshot);
    }
    if (worklist)
        MVM_spesh_plugin_guard_list_mark(tc, tc->plugin_guards, tc->num_plugin_guards, worklist);
    if (tc->temp_plugin_guards)
        MVM_spesh_plugin_guard_list_mark(tc, tc->temp_plugin_guards, tc->temp_num_plugin_guards, worklist);
    add_collectable(tc, worklist, snapshot, tc->plugin_guard_args,
        "Plugin guard args");
    if (tc->temp_plugin_guard_args)
        add_collectable(tc, worklist, snapshot, tc->temp_plugin_guard_args,
            "Temporary plugin guard args");

    if (tc->step_mode_frame)
        add_collectable(tc, worklist, snapshot, tc->step_mode_frame,
                "Frame referenced for stepping mode");
}

/* Pushes a temporary root onto the thread-local roots list. */
void MVM_gc_root_temp_push_slow(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    /* Allocate extra temporary root space if needed. */
    if (tc->num_temproots == tc->alloc_temproots) {
        tc->alloc_temproots *= 2;
        tc->temproots = MVM_realloc(tc->temproots,
            sizeof(MVMCollectable **) * tc->alloc_temproots);
    }

    /* Add this one to the list. */
    tc->temproots[tc->num_temproots] = obj_ref;
    tc->num_temproots++;
}

/* Marks the temporary root stack at its current height as the limit for
 * removing all roots. This is done so that in nested interpreter runs
 * (at present, just nativecall callbacks) we don't clear things that
 * are pushed by the native call itself. */
MVMuint32 MVM_gc_root_temp_mark(MVMThreadContext *tc) {
    MVMint32 current = tc->mark_temproots;
    tc->mark_temproots = tc->num_temproots;
    return current;
}

/* Resets the temporary root stack mark to the provided height. */
void MVM_gc_root_temp_mark_reset(MVMThreadContext *tc, MVMuint32 mark) {
    tc->mark_temproots = mark;
}

/* Pops all temporary roots off the thread-local roots list. */
void MVM_gc_root_temp_pop_all(MVMThreadContext *tc) {
    tc->num_temproots = tc->mark_temproots;
}

/* Adds the set of thread-local temporary roots to a GC worklist. Note that we
 * may MVMROOT things that are actually frames on a therad local call stack as
 * they may be GC-able; check for this and make sure such roots do not get
 * added to the worklist. (Cheaper to do it here in the event we GC than to
 * do it on every stack push). */
static MVMuint32 is_stack_frame(MVMThreadContext *tc, MVMCollectable **c) {
    MVMCollectable *maybe_frame = *c;
    return maybe_frame && maybe_frame->flags1 == 0 && maybe_frame->owner == 0;
}
void MVM_gc_root_add_temps_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMuint32         i, num_roots;
    MVMCollectable ***temproots;
    num_roots = tc->num_temproots;
    temproots = tc->temproots;
    if (worklist) {
        for (i = 0; i < num_roots; i++)
            if (!is_stack_frame(tc, temproots[i]))
                MVM_gc_worklist_add(tc, worklist, temproots[i]);
    }
    else {
        for (i = 0; i < num_roots; i++)
            if (!is_stack_frame(tc, temproots[i]))
                MVM_profile_heap_add_collectable_rel_idx(tc, snapshot, *(temproots[i]), i);
    }
}

/* Pushes a collectable that is in generation 2, but now references a nursery
 * collectable, into the gen2 root set. */
void MVM_gc_root_gen2_add(MVMThreadContext *tc, MVMCollectable *c) {
    /* Ensure the collectable is not null. */
    if (c == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null collectable address as an inter-generational root");
    assert(!(c->flags2 & MVM_CF_FORWARDER_VALID));

    /* Allocate extra gen2 aggregate space if needed. */
    if (tc->num_gen2roots == tc->alloc_gen2roots) {
        tc->alloc_gen2roots *= 2;
        tc->gen2roots = MVM_realloc(tc->gen2roots,
            sizeof(MVMCollectable *) * tc->alloc_gen2roots);
    }

    /* Add this one to the list. */
    tc->gen2roots[tc->num_gen2roots] = c;
    tc->num_gen2roots++;

    /* Flag it as added, so we don't add it multiple times. */
    c->flags2 |= MVM_CF_IN_GEN2_ROOT_LIST;
}

/* Adds the set of thread-local inter-generational roots to a GC worklist. As
 * a side-effect, removes gen2 roots that no longer point to any nursery
 * items (usually because all the referenced objects also got promoted). */
void MVM_gc_root_add_gen2s_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMCollectable **gen2roots = tc->gen2roots;
    MVMuint32        num_roots = tc->num_gen2roots;
    MVMuint32        i;

    /* We'll remove some entries from this list. The algorithm is simply to
     * slide all that stay towards the start of the array. */
    MVMuint32 insert_pos = 0;

    /* Guess that we'll end up with around num_roots entries, to avoid some
     * worklist growth reallocations. */
    MVM_gc_worklist_presize_for(tc, worklist, num_roots);

    /* Visit each gen2 root and... */
    for (i = 0; i < num_roots; i++) {
        /* Count items on worklist before we mark it. */
        MVMuint32 items_before_mark  = worklist->items;

        /* Put things it references into the worklist; since the worklist will
         * be set not to include gen2 things, only nursery things will make it
         * in. */
        assert(!(gen2roots[i]->flags2 & MVM_CF_FORWARDER_VALID));
        MVM_gc_mark_collectable(tc, worklist, gen2roots[i]);

        /* If we added any nursery objects, or if we are a frame with ->work
         * area, keep in this list. */
        if (worklist->items != items_before_mark ||
                (gen2roots[i]->flags1 & MVM_CF_FRAME && ((MVMFrame *)gen2roots[i])->work)) {
            gen2roots[insert_pos] = gen2roots[i];
            insert_pos++;
        }

        /* Otherwise, clear the "in gen2 root list" flag. Note that another
         * thread may also clear this flag if it also had the entry in its
         * inter-gen list, so be careful to clear it, not just toggle. */
        else {
            gen2roots[i]->flags2 &= ~MVM_CF_IN_GEN2_ROOT_LIST;
        }
    }

    /* New number of entries after sliding is the final insert position. */
    tc->num_gen2roots = insert_pos;
}

/* Adds inter-generational roots to a heap snapshot. */
void MVM_gc_root_add_gen2s_to_snapshot(MVMThreadContext *tc, MVMHeapSnapshotState *snapshot) {
    MVMCollectable **gen2roots = tc->gen2roots;
    MVMuint32        num_roots = tc->num_gen2roots;
    MVMuint32        i;
    for (i = 0; i < num_roots; i++)
        MVM_profile_heap_add_collectable_rel_idx(tc, snapshot, gen2roots[i], i);
}

/* Visits all of the roots in the gen2 list and removes those that have been
 * collected. Applied after a full collection. */
void MVM_gc_root_gen2_cleanup(MVMThreadContext *tc) {
    MVMCollectable **gen2roots    = tc->gen2roots;
    MVMuint32        num_roots    = tc->num_gen2roots;
    MVMuint32        i = 0;
    MVMuint32        cur_survivor;

    /* Find the first collected object. */
    while (i < num_roots && gen2roots[i]->flags2 & MVM_CF_GEN2_LIVE)
        i++;
    cur_survivor = i;

    /* Slide others back so the alive ones are at the start of the list. */
    while (i < num_roots) {
        if (gen2roots[i]->flags2 & MVM_CF_GEN2_LIVE) {
            assert(!(gen2roots[i]->flags2 & MVM_CF_FORWARDER_VALID));
            gen2roots[cur_survivor++] = gen2roots[i];
        }
        i++;
    }

    tc->num_gen2roots = cur_survivor;
}

/* Walks frames and compilation units. Adds the roots it finds into the
 * GC worklist. */
static void scan_lexicals(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *cur_frame) {
    /* Add caller to worklist if it's heap-allocated. */
    if (cur_frame->caller && !MVM_FRAME_IS_ON_CALLSTACK(tc, cur_frame->caller))
        MVM_gc_worklist_add(tc, worklist, &cur_frame->caller);

    /* Add outer, code_ref and static info to work list. */
    MVM_gc_worklist_add(tc, worklist, &cur_frame->outer);
    MVM_gc_worklist_add(tc, worklist, &cur_frame->code_ref);
    MVM_gc_worklist_add(tc, worklist, &cur_frame->static_info);
    MVM_gc_worklist_add(tc, worklist, &cur_frame->spesh_cand);

    /* Mark frame extras if needed. */
    if (cur_frame->extra) {
        MVMFrameExtra *e = cur_frame->extra;
        if (e->special_return_data && e->mark_special_return_data)
            e->mark_special_return_data(tc, cur_frame, worklist);
        if (e->continuation_tags) {
            MVMContinuationTag *tag = e->continuation_tags;
            while (tag) {
                MVM_gc_worklist_add(tc, worklist, &tag->tag);
                tag = tag->next;
            }
        }
        MVM_gc_worklist_add(tc, worklist, &e->invoked_call_capture);
        if (e->dynlex_cache_name)
            MVM_gc_worklist_add(tc, worklist, &e->dynlex_cache_name);
        MVM_gc_worklist_add(tc, worklist, &e->exit_handler_result);
    }

    /* Scan the registers. */
    MVM_gc_root_add_frame_registers_to_worklist(tc, worklist, cur_frame);
    scan_lexicals(tc, worklist, cur_frame);
}

/* Takes a frame, scans its registers and adds them to the roots. */
void MVM_gc_root_add_frame_registers_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    MVMuint16  i, count, flag;
    MVMuint16 *type_map;
    MVMuint8  *flag_map;
    /* We only need to do any of this work if the frame is in dynamic scope. */
    if (frame->work) {
        /* Scan locals. */

        MVMSpeshCandidate *spesh_cand = frame->spesh_cand;
        MVMJitCode *jitcode = spesh_cand ? spesh_cand->body.jitcode : NULL;
        if (jitcode && jitcode->local_types) {
            type_map = jitcode->local_types;
            count    = jitcode->num_locals;
        } else if (spesh_cand && spesh_cand->body.local_types) {
            type_map = spesh_cand->body.local_types;
            count    = spesh_cand->body.num_locals;
        }
        else {
            type_map = frame->static_info->body.local_types;
            count    = frame->static_info->body.num_locals;
        }
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &frame->work[i].o);

        /* Scan arg buffer if needed. */
        if (frame->cur_args_callsite) {
            flag_map = frame->cur_args_callsite->arg_flags;
            count = frame->cur_args_callsite->arg_count;
            for (i = 0, flag = 0; i < count; i++, flag++) {
                if (flag_map[flag] & MVM_CALLSITE_ARG_NAMED) {
                    /* Current position is name, then next is value. */
                    MVM_gc_worklist_add(tc, worklist, &frame->args[i].s);
                    i++;
                }
                if (flag_map[flag] & MVM_CALLSITE_ARG_STR || flag_map[flag] & MVM_CALLSITE_ARG_OBJ)
                    MVM_gc_worklist_add(tc, worklist, &frame->args[i].o);
            }
        }

        /* Scan arguments in case there was a flattening. Don't need to if
         * there wasn't a flattening because orig args is a subset of locals. */
        /* Will not be needed after we have moved flattening up front and legacy
         * args handling is removed. */
        if (frame->params.version == MVM_ARGS_LEGACY &&
                frame->params.legacy.arg_flags && frame->params.legacy.callsite->has_flattening) {
            MVMArgProcContext *ctx = &frame->params;
            flag_map = ctx->legacy.arg_flags;
            count = ctx->legacy.arg_count;
            for (i = 0, flag = 0; i < count; i++, flag++) {
                if (flag_map[flag] & MVM_CALLSITE_ARG_NAMED) {
                    /* Current position is name, then next is value. */
                    MVM_gc_worklist_add(tc, worklist, &ctx->legacy.args[i].s);
                    i++;
                }
                if (flag_map[flag] & MVM_CALLSITE_ARG_STR || flag_map[flag] & MVM_CALLSITE_ARG_OBJ)
                    MVM_gc_worklist_add(tc, worklist, &ctx->legacy.args[i].o);
            }
        }
    }
}

/* Takes a frame, scans its lexicals and adds them to the roots. */
static void scan_lexicals(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    if (frame->env) {
        MVMuint16  i, count;
        MVMuint16 *type_map;
        if (frame->spesh_cand && frame->spesh_cand->body.lexical_types) {
            type_map = frame->spesh_cand->body.lexical_types;
            count    = frame->spesh_cand->body.num_lexicals;
        }
        else {
            type_map = frame->static_info->body.lexical_types;
            count    = frame->static_info->body.num_lexicals;
        }
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &frame->env[i].o);
    }
}
