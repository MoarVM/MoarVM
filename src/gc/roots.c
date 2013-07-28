#include "moarvm.h"

static void scan_registers(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame);

/* Adds a location holding a collectable object to the permanent list of GC
 * roots, so that it will always be marked and never die. Note that the
 * address of the collectable must be passed, since it will need to be
 * updated. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as a permanent root");

    if (apr_thread_mutex_lock(tc->instance->mutex_permroots) == APR_SUCCESS) {
        /* Allocate extra permanent root space if needed. */
        if (tc->instance->num_permroots == tc->instance->alloc_permroots) {
            tc->instance->alloc_permroots *= 2;
            tc->instance->permroots = realloc(tc->instance->permroots,
                sizeof(MVMCollectable **) * tc->instance->alloc_permroots);
        }

        /* Add this one to the list. */
        tc->instance->permroots[tc->instance->num_permroots] = obj_ref;
        tc->instance->num_permroots++;

        if (apr_thread_mutex_unlock(tc->instance->mutex_permroots) != APR_SUCCESS)
            MVM_panic(MVM_exitcode_gcroots, "Unable to unlock GC permanent root mutex");
    }
    else {
        MVM_panic(MVM_exitcode_gcroots, "Unable to lock GC permanent root mutex");
    }
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
    MVM_gc_worklist_add(tc, worklist, &tc->instance->threads);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->compiler_registry);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->hll_syms);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->clargs);
}

/* Adds anything that is a root thanks to being referenced by a thread,
 * context, but that isn't permanent. */
void MVM_gc_root_add_tc_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    /* Any active exception handlers. */
    MVMActiveHandler *cur_ah = tc->active_handlers;
    while (cur_ah != NULL) {
        MVM_gc_worklist_add(tc, worklist, &cur_ah->ex_obj);
    }

    /* The usecapture object. */
    MVM_gc_worklist_add(tc, worklist, &tc->cur_usecapture);
}

/* Pushes a temporary root onto the thread-local roots list. */
void MVM_gc_root_temp_push(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    /* Ensure the root is not null. */
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as a temporary root");

    /* Allocate extra temporary root space if needed. */
    if (tc->num_temproots == tc->alloc_temproots) {
        tc->alloc_temproots *= 2;
        tc->temproots = realloc(tc->temproots,
            sizeof(MVMCollectable **) * tc->alloc_temproots);
    }

    /* Add this one to the list. */
    tc->temproots[tc->num_temproots] = obj_ref;
    tc->num_temproots++;
}

/* Pops a temporary root off the thread-local roots list. */
void MVM_gc_root_temp_pop(MVMThreadContext *tc) {
    if (tc->num_temproots > 0)
        tc->num_temproots--;
    else
        MVM_panic(1, "Illegal attempt to pop empty temporary root stack");
}

/* Pops temporary roots off the thread-local roots list. */
void MVM_gc_root_temp_pop_n(MVMThreadContext *tc, MVMuint32 n) {
    if (tc->num_temproots >= n)
        tc->num_temproots -= n;
    else
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to pop insufficiently large temporary root stack");
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

    /* Allocate extra gen2 aggregate space if needed. */
    if (tc->num_gen2roots == tc->alloc_gen2roots) {
        tc->alloc_gen2roots *= 2;
        tc->gen2roots = realloc(tc->gen2roots,
            sizeof(MVMCollectable **) * tc->alloc_gen2roots);
    }

    /* Add this one to the list. */
    tc->gen2roots[tc->num_gen2roots] = c;
    tc->num_gen2roots++;

    /* Flag it as added, so we don't add it multiple times. */
    c->flags |= MVM_CF_IN_GEN2_ROOT_LIST;
}

/* Adds the set of thread-local inter-generational roots to a GC worklist. */
void MVM_gc_root_add_gen2s_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMCollectable **gen2roots = tc->gen2roots;
    MVMuint32        num_roots = tc->num_gen2roots;
    MVMuint32        i;

    /* Mark gen2 objects that point to nursery things. */
    for (i = 0; i < num_roots; i++)
        MVM_gc_mark_collectable(tc, worklist, gen2roots[i]);
}

/* Visits all of the roots in the gen2 list and removes those that have been
 * collected. Applied after a full collection. */
void MVM_gc_root_gen2_cleanup(MVMThreadContext *tc) {
    MVMCollectable **gen2roots    = tc->gen2roots;
    MVMuint32        num_roots    = tc->num_gen2roots;
    MVMuint32        cur_survivor = 0;
    MVMuint32        i;
    for (i = 0; i < num_roots; i++)
        if (gen2roots[i]->forwarder)
            gen2roots[cur_survivor++] = gen2roots[i]->forwarder;
    tc->num_gen2roots = cur_survivor;
}

/* Walks frames and compilation units. Adds the roots it finds into the
 * GC worklist. */
void MVM_gc_root_add_frame_roots_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *start_frame) {
    MVMint8 did_something = 1;

    /* Create processing lists for frames, static frames and compilation
     * units. (Yeah, playing fast and loose with types here...) */
    MVMGCWorklist *frame_worklist        = MVM_gc_worklist_create(tc);
    MVMGCWorklist *static_frame_worklist = MVM_gc_worklist_create(tc);
    MVMGCWorklist *compunit_worklist     = MVM_gc_worklist_create(tc);

    /* We'll iterate until everything reachable has the current sequence
     * number. */
    MVMuint32 cur_seq_number = tc->instance->gc_seq_number;

    /* Place current frame on the frame worklist. */
    MVM_gc_worklist_add(tc, frame_worklist, start_frame);

    /* XXX For now we also put on all compilation units. This is a hack,
     * as it means compilation units never die. */
    {
        MVMCompUnit *cur_cu = tc->instance->head_compunit;
        while (cur_cu) {
            MVM_gc_worklist_add(tc, compunit_worklist, cur_cu);
            cur_cu = cur_cu->next_compunit;
        }
    }

    /* Iterate while we scan all the things. */
    while (did_something) {
        MVMFrame       *cur_frame;
        MVMStaticFrame *cur_static_frame;
        MVMCompUnit    *cur_compunit;

        /* Reset flag. */
        did_something = 0;

        /* Handle any frames in the work list. */
        while ((cur_frame = (MVMFrame *)MVM_gc_worklist_get(tc, frame_worklist))) {
            /* If we already saw the frame this run, skip it. */
            MVMuint32 orig_seq = cur_frame->gc_seq_number;
            if (orig_seq == cur_seq_number)
                continue;
            if (apr_atomic_cas32(&cur_frame->gc_seq_number, cur_seq_number, orig_seq) != orig_seq)
                continue;

            /* Add static frame to work list if needed. */
            if (cur_frame->static_info->gc_seq_number != cur_seq_number)
                MVM_gc_worklist_add(tc, static_frame_worklist, cur_frame->static_info);

            /* Add caller and outer to frames work list. */
            if (cur_frame->caller)
                MVM_gc_worklist_add(tc, frame_worklist, cur_frame->caller);
            if (cur_frame->outer)
                MVM_gc_worklist_add(tc, frame_worklist, cur_frame->outer);

            /* add code_ref to work list unless we're the top-level frame. */
            if (cur_frame->code_ref)
                MVM_gc_worklist_add(tc, worklist, &cur_frame->code_ref);

            /* Scan the registers. */
            scan_registers(tc, worklist, cur_frame);

            /* Mark that we did some work (and thus possibly have more work
             * to do later). */
            did_something = 1;
        }

        /* Handle any static frames in the work list. */
        while ((cur_static_frame = (MVMStaticFrame *)MVM_gc_worklist_get(tc, static_frame_worklist))) {
            /* If we already saw the static frame this run, skip it. */
            MVMuint32 orig_seq = cur_static_frame->gc_seq_number;
            if (orig_seq == cur_seq_number)
                continue;
            if (apr_atomic_cas32(&cur_static_frame->gc_seq_number, cur_seq_number, orig_seq) != orig_seq)
                continue;

            /* Add compilation unit to worklist if needed. */
            if (cur_static_frame->cu->gc_seq_number != cur_seq_number)
                MVM_gc_worklist_add(tc, compunit_worklist, cur_static_frame->cu);

            /* Add name and ID strings to GC worklist. */
            MVM_gc_worklist_add(tc, worklist, &cur_static_frame->cuuid);
            MVM_gc_worklist_add(tc, worklist, &cur_static_frame->name);

            /* Add prior invocation, if any. */
            if (cur_static_frame->prior_invocation)
                MVM_gc_worklist_add(tc, frame_worklist, cur_static_frame->prior_invocation);

            /* Scan static lexicals. */
            if (cur_static_frame->static_env) {
                MVMuint16 *type_map = cur_static_frame->lexical_types;
                MVMuint16  count    = cur_static_frame->num_lexicals;
                MVMuint16  i;
                for (i = 0; i < count; i++)
                    if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                        MVM_gc_worklist_add(tc, worklist, &cur_static_frame->static_env[i].o);
            }

            /* Mark that we did some work (and thus possibly have more work
             * to do later). */
            did_something = 1;
        }

        /* Handle any compilation units in the work list. */
        while ((cur_compunit = (MVMCompUnit *)MVM_gc_worklist_get(tc, compunit_worklist))) {
            MVMuint32 i;

            /* If we already saw the compunit this run, skip it. */
            MVMuint32 orig_seq = cur_compunit->gc_seq_number;
            if (orig_seq == cur_seq_number)
                continue;
            if (apr_atomic_cas32(&cur_compunit->gc_seq_number, cur_seq_number, orig_seq) != orig_seq)
                continue;

            /* Add code refs and static frames to the worklists. */
            for (i = 0; i < cur_compunit->num_frames; i++) {
                MVM_gc_worklist_add(tc, static_frame_worklist, cur_compunit->frames[i]);
                MVM_gc_worklist_add(tc, worklist, &cur_compunit->coderefs[i]);
            }

            /* Add strings to the worklists. */
            for (i = 0; i < cur_compunit->num_strings; i++)
                MVM_gc_worklist_add(tc, worklist, &cur_compunit->strings[i]);

            /* Add serialization contexts to the worklist. */
            for (i = 0; i < cur_compunit->num_scs; i++) {
                if (cur_compunit->scs[i])
                    MVM_gc_worklist_add(tc, worklist, &cur_compunit->scs[i]);
                if (cur_compunit->scs_to_resolve[i])
                    MVM_gc_worklist_add(tc, worklist, &cur_compunit->scs_to_resolve[i]);
            }

            /* Add various other referenced strings, etc. */
            MVM_gc_worklist_add(tc, worklist, &cur_compunit->hll_name);
            MVM_gc_worklist_add(tc, worklist, &cur_compunit->filename);

            /* Mark that we did some work (and thus possibly have more work
             * to do later). */
            did_something = 1;
        }
    }

    /* Clean up frame and compunit lists. */
    MVM_gc_worklist_destroy(tc, frame_worklist);
    MVM_gc_worklist_destroy(tc, static_frame_worklist);
    MVM_gc_worklist_destroy(tc, compunit_worklist);
}

/* Takes a frame, scans its registers and adds them to the roots. */
static void scan_registers(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    MVMuint16  i, count;
    MVMuint16 *type_map;
    MVMuint8  *flag_map;

    /* Scan locals. */
    if (frame->work && frame->tc) {
        type_map = frame->static_info->local_types;
        count    = frame->static_info->num_locals;
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &frame->work[i].o);
    }

    /* Scan lexicals. */
    if (frame->env) {
        type_map = frame->static_info->lexical_types;
        count    = frame->static_info->num_lexicals;
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &frame->env[i].o);
    }

    /* Scan arguments in case there was a flattening. Don't need to if
     * there wasn't a flattening because orig args is a subset of locals. */
    if (frame->params.args && frame->params.callsite->has_flattening) {
        MVMArgProcContext *ctx = &frame->params;
        flag_map = ctx->arg_flags;
        count = ctx->arg_count;
        for (i = 0; i < count; i++)
            if (flag_map[i] & MVM_CALLSITE_ARG_STR || flag_map[i] & MVM_CALLSITE_ARG_OBJ)
                MVM_gc_worklist_add(tc, worklist, &ctx->args[i].o);
    }
}
