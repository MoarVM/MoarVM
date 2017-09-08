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
    MVMSerializationContextBody *current, *tmp;
    MVMLoadedCompUnitName       *current_lcun, *tmp_lcun;
    unsigned                     bucket_tmp;
    MVMString                  **int_to_str_cache;
    MVMuint32                    i;

    add_collectable(tc, worklist, snapshot, tc->instance->threads, "Thread list");
    add_collectable(tc, worklist, snapshot, tc->instance->compiler_registry, "Compiler registry");
    add_collectable(tc, worklist, snapshot, tc->instance->hll_syms, "HLL symbols");
    add_collectable(tc, worklist, snapshot, tc->instance->clargs, "Command line args");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_todo_queue,
        "Event loop todo queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_permit_queue,
        "Event loop permit queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_cancel_queue,
        "Event loop cancel queue");
    add_collectable(tc, worklist, snapshot, tc->instance->event_loop_active, "Event loop active");

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
    HASH_ITER(hash_handle, tc->instance->sc_weakhash, current, tmp, bucket_tmp) {
        /* mark the string handle pointer iff it hasn't yet been resolved */
        add_collectable(tc, worklist, snapshot, current->hash_handle.key,
            "SC weakhash hash key");
        if (!current->sc)
            add_collectable(tc, worklist, snapshot, current->handle,
                "SC weakhash unresolved handle");
        else if (!current->claimed)
            add_collectable(tc, worklist, snapshot, current->sc,
                "SC weakhash unclaimed SC");
    }

    HASH_ITER(hash_handle, tc->instance->loaded_compunits, current_lcun, tmp_lcun, bucket_tmp) {
        add_collectable(tc, worklist, snapshot, current_lcun->hash_handle.key,
            "Loaded compilation unit hash key");
        add_collectable(tc, worklist, snapshot, current_lcun->filename,
            "Loaded compilation unit filename");
    }

    add_collectable(tc, worklist, snapshot, tc->instance->cached_backend_config,
        "Cached backend configuration hash");
}

/* Adds anything that is a root thanks to being referenced by a thread,
 * context, but that isn't permanent. */
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMNativeCallbackCacheHead *current_cbceh, *tmp_cbceh;
    unsigned bucket_tmp;

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

    /* Callback cache. */
    HASH_ITER(hash_handle, tc->native_callback_cache, current_cbceh, tmp_cbceh, bucket_tmp) {
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
    }

    /* Profiling data. */
    if (worklist)
        MVM_profile_instrumented_mark_data(tc, worklist);

    /* Serialized string heap, if any. */
    add_collectable(tc, worklist, snapshot, tc->serialized_string_heap,
        "Serialized string heap");

    /* Specialization log and stack simulation. */
    add_collectable(tc, worklist, snapshot, tc->spesh_log, "Specialization log");
    if (worklist)
        MVM_spesh_sim_stack_gc_mark(tc, tc->spesh_sim_stack, worklist);
    else {
        MVM_spesh_sim_stack_gc_describe(tc, snapshot, tc->spesh_sim_stack);
    }
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
    return maybe_frame && maybe_frame->flags == 0 && maybe_frame->owner == 0;
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
    assert(!(c->flags & MVM_CF_FORWARDER_VALID));

    /* Allocate extra gen2 aggregate space if needed. */
    if (tc->num_gen2roots == tc->alloc_gen2roots) {
        tc->alloc_gen2roots *= 2;
        tc->gen2roots = MVM_realloc(tc->gen2roots,
            sizeof(MVMCollectable **) * tc->alloc_gen2roots);
    }

    /* Add this one to the list. */
    tc->gen2roots[tc->num_gen2roots] = c;
    tc->num_gen2roots++;

    /* Flag it as added, so we don't add it multiple times. */
    c->flags |= MVM_CF_IN_GEN2_ROOT_LIST;
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
        assert(!(gen2roots[i]->flags & MVM_CF_FORWARDER_VALID));
        MVM_gc_mark_collectable(tc, worklist, gen2roots[i]);

        /* If we added any nursery objects, or if we are a frame with ->work
         * area, keep in this list. */
        if (worklist->items != items_before_mark ||
                (gen2roots[i]->flags & MVM_CF_FRAME && ((MVMFrame *)gen2roots[i])->work)) {
            gen2roots[insert_pos] = gen2roots[i];
            insert_pos++;
        }

        /* Otherwise, clear the "in gen2 root list" flag. Note that another
         * thread may also clear this flag if it also had the entry in its
         * inter-gen list, so be careful to clear it, not just toggle. */
        else {
            gen2roots[i]->flags &= ~MVM_CF_IN_GEN2_ROOT_LIST;
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
    while (i < num_roots && gen2roots[i]->flags & MVM_CF_GEN2_LIVE)
        i++;
    cur_survivor = i;

    /* Slide others back so the alive ones are at the start of the list. */
    while (i < num_roots) {
        if (gen2roots[i]->flags & MVM_CF_GEN2_LIVE) {
            assert(!(gen2roots[i]->flags & MVM_CF_FORWARDER_VALID));
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
        if (frame->spesh_cand && frame->spesh_cand->local_types) {
            type_map = frame->spesh_cand->local_types;
            count    = frame->spesh_cand->num_locals;
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
        if (frame->params.arg_flags && frame->params.callsite->has_flattening) {
            MVMArgProcContext *ctx = &frame->params;
            flag_map = ctx->arg_flags;
            count = ctx->arg_count;
            for (i = 0, flag = 0; i < count; i++, flag++) {
                if (flag_map[flag] & MVM_CALLSITE_ARG_NAMED) {
                    /* Current position is name, then next is value. */
                    MVM_gc_worklist_add(tc, worklist, &ctx->args[i].s);
                    i++;
                }
                if (flag_map[flag] & MVM_CALLSITE_ARG_STR || flag_map[flag] & MVM_CALLSITE_ARG_OBJ)
                    MVM_gc_worklist_add(tc, worklist, &ctx->args[i].o);
            }
        }
    }
}

/* Takes a frame, scans its lexicals and adds them to the roots. */
static void scan_lexicals(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    if (frame->env) {
        MVMuint16  i, count;
        MVMuint16 *type_map;
        if (frame->spesh_cand && frame->spesh_cand->lexical_types) {
            type_map = frame->spesh_cand->lexical_types;
            count    = frame->spesh_cand->num_lexicals;
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
