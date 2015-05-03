#include "moar.h"

static void scan_registers(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);

/* Adds a location holding a collectable object to the permanent list of GC
 * roots, so that it will always be marked and never die. Note that the
 * address of the collectable must be passed, since it will need to be
 * updated. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as a permanent root");

    uv_mutex_lock(&tc->instance->mutex_permroots);
    /* Allocate extra permanent root space if needed. */
    if (tc->instance->num_permroots == tc->instance->alloc_permroots) {
        tc->instance->alloc_permroots *= 2;
        tc->instance->permroots = MVM_realloc(tc->instance->permroots,
            sizeof(MVMCollectable **) * tc->instance->alloc_permroots);
    }

    /* Add this one to the list. */
    tc->instance->permroots[tc->instance->num_permroots] = obj_ref;
    tc->instance->num_permroots++;

    uv_mutex_unlock(&tc->instance->mutex_permroots);
}

/* Adds the set of permanently registered roots to a GC worklist. */
void MVM_gc_root_add_permanents_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMuint32         i, num_roots;
    MVMCollectable ***permroots;
    num_roots = tc->instance->num_permroots;
    permroots = tc->instance->permroots;
    for (i = 0; i < num_roots; i++)
        MVM_gc_worklist_add(tc, worklist, permroots[i]);
}

/* Adds anything that is a root thanks to being referenced by instance,
 * but that isn't permanent. */
void MVM_gc_root_add_instance_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMSerializationContextBody *current, *tmp;
    MVMLoadedCompUnitName       *current_lcun, *tmp_lcun;
    unsigned                     bucket_tmp;
    MVMString                  **int_to_str_cache;
    MVMuint32                    i;

    MVM_gc_worklist_add(tc, worklist, &tc->instance->threads);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->compiler_registry);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->hll_syms);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->clargs);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->event_loop_todo_queue);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->event_loop_cancel_queue);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->event_loop_active);

    int_to_str_cache = tc->instance->int_to_str_cache;
    for (i = 0; i < MVM_INT_TO_STR_CACHE_SIZE; i++)
        MVM_gc_worklist_add(tc, worklist, &(int_to_str_cache[i]));

    /* okay, so this makes the weak hash slightly less weak.. for certain
     * keys of it anyway... */
    HASH_ITER(hash_handle, tc->instance->sc_weakhash, current, tmp, bucket_tmp) {
        /* mark the string handle pointer iff it hasn't yet been resolved */
        if (!current->sc)
            MVM_gc_worklist_add(tc, worklist, &current->handle);
    }

    HASH_ITER(hash_handle, tc->instance->loaded_compunits, current_lcun, tmp_lcun, bucket_tmp) {
        MVM_gc_worklist_add(tc, worklist, &current_lcun->filename);
    }

    MVM_gc_worklist_add(tc, worklist, &tc->instance->cached_backend_config);
}

/* Adds anything that is a root thanks to being referenced by a thread,
 * context, but that isn't permanent. */
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMNativeCallbackCacheHead *current_cbceh, *tmp_cbceh;
    unsigned bucket_tmp;

    /* Any active exception handlers. */
    MVMActiveHandler *cur_ah = tc->active_handlers;
    while (cur_ah != NULL) {
        MVM_gc_worklist_add(tc, worklist, &cur_ah->ex_obj);
        cur_ah = cur_ah->next_handler;
    }

    /* Any exception handler result. */
    MVM_gc_worklist_add(tc, worklist, &tc->last_handler_result);

    /* The usecapture object. */
    MVM_gc_worklist_add(tc, worklist, &tc->cur_usecapture);

    /* List of SCs currently being compiled. */
    MVM_gc_worklist_add(tc, worklist, &tc->compiling_scs);

    /* compunit variable pointer (and be null if thread finished) */
    if (tc->interp_cu)
        MVM_gc_worklist_add(tc, worklist, tc->interp_cu);

    /* Lexotics cache. */
    if (tc->lexotic_cache_size) {
        MVMuint32 i;
        for (i = 0; i < tc->lexotic_cache_size; i++)
            if (tc->lexotic_cache[i])
                MVM_gc_worklist_add(tc, worklist, &(tc->lexotic_cache[i]));
    }

    /* Current dispatcher. */
    MVM_gc_worklist_add(tc, worklist, &tc->cur_dispatcher);

    /* Callback cache. */
    HASH_ITER(hash_handle, tc->native_callback_cache, current_cbceh, tmp_cbceh, bucket_tmp) {
        MVMint32 i;
        MVMNativeCallback *entry = current_cbceh->head;
        while (entry) {
            for (i = 0; i < entry->num_types; i++)
                MVM_gc_worklist_add(tc, worklist, &(entry->types[i]));
            MVM_gc_worklist_add(tc, worklist, &(entry->target));
            entry = entry->next;
        }
    }

    /* Profiling data. */
    MVM_profile_mark_data(tc, worklist);

    /* Serialized string heap, if any. */
    MVM_gc_worklist_add(tc, worklist, &tc->serialized_string_heap);
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

/* Adds the set of thread-local temporary roots to a GC worklist. */
void MVM_gc_root_add_temps_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMuint32         i, num_roots;
    MVMCollectable ***temproots;
    num_roots = tc->num_temproots;
    temproots = tc->temproots;
    for (i = 0; i < num_roots; i++)
        MVM_gc_worklist_add(tc, worklist, temproots[i]);
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
        MVMuint32 frames_before_mark = worklist->frames;

        /* Put things it references into the worklist; since the worklist will
         * be set not to include gen2 things, only nursery things will make it
         * in. */
        assert(!(gen2roots[i]->flags & MVM_CF_FORWARDER_VALID));
        MVM_gc_mark_collectable(tc, worklist, gen2roots[i]);

        /* If we added any nursery objects or frames, or if we are marked as
         * referencing frames, then we need to stay in this list. */
        if (worklist->items != items_before_mark ||
                worklist->frames != frames_before_mark ||
                (!(gen2roots[i]->flags & MVM_CF_STABLE) && REPR(gen2roots[i])->refs_frames)) {
            gen2roots[insert_pos] = gen2roots[i];
            insert_pos++;
        }

        /* Otherwise, clear the "in gen2 root list" flag. */
        else {
            gen2roots[i]->flags ^= MVM_CF_IN_GEN2_ROOT_LIST;
        }
    }

    /* New number of entries after sliding is the final insert position. */
    tc->num_gen2roots = insert_pos;
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
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *start_frame) {
    MVMFrame *cur_frame = start_frame;
    MVMuint32 cur_seq_number = MVM_load(&tc->instance->gc_seq_number);
    /* If we already saw the frame this run, skip it. */
    MVMuint32 orig_seq = MVM_load(&cur_frame->gc_seq_number);
    if (orig_seq == cur_seq_number)
        return;
    if (MVM_cas(&cur_frame->gc_seq_number, orig_seq, cur_seq_number) != orig_seq)
        return;

    /* Add caller and outer to frames work list. */
    MVM_gc_worklist_add_frame(tc, worklist, cur_frame->caller);
    MVM_gc_worklist_add_frame(tc, worklist, cur_frame->outer);

    /* add code_ref to work list unless we're the top-level frame. */
    if (cur_frame->code_ref)
        MVM_gc_worklist_add(tc, worklist, &cur_frame->code_ref);
    MVM_gc_worklist_add(tc, worklist, &cur_frame->static_info);

    /* Add any context object. */
    if (cur_frame->context_object)
        MVM_gc_worklist_add(tc, worklist, &cur_frame->context_object);

    /* Mark special return data, if needed. */
    if (cur_frame->special_return_data && cur_frame->mark_special_return_data)
        cur_frame->mark_special_return_data(tc, cur_frame, worklist);

    /* Mark any continuation tags. */
    if (cur_frame->continuation_tags) {
        MVMContinuationTag *tag = cur_frame->continuation_tags;
        while (tag) {
            MVM_gc_worklist_add(tc, worklist, &tag->tag);
            tag = tag->next;
        }
    }

    /* Mark any dyn lex cache. */
    if (cur_frame->dynlex_cache_name)
        MVM_gc_worklist_add(tc, worklist, &cur_frame->dynlex_cache_name);

    /* Scan the registers. */
    scan_registers(tc, worklist, cur_frame);
}

/* Takes a frame, scans its registers and adds them to the roots. */
static void scan_registers(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    MVMuint16  i, count, flag;
    MVMuint16 *type_map;
    MVMuint8  *flag_map;

    /* Scan locals. */
    if (frame->work && frame->tc) {
        if (frame->spesh_cand && frame->spesh_log_idx == -1 && frame->spesh_cand->local_types) {
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
    }

    /* Scan arg buffer if needed. */
    if (frame->args && frame->cur_args_callsite) {
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

    /* Scan lexicals. */
    if (frame->env) {
        if (frame->spesh_cand && frame->spesh_log_idx == -1 && frame->spesh_cand->lexical_types) {
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
