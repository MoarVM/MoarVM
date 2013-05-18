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
    if (tc->num_temproots - n >= 0)
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

/* Pushes a root onto the inter-generational roots list. The memory address
 * must stay valid for the duration of the enclosing object's lifetime.  */
void MVM_gc_root_gen2_ref_add(MVMThreadContext *tc, MVMCollectable **obj_ref) {
    /* Ensure the root is not null. */
    if (obj_ref == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null object address as an inter-generational root");
    
    /* Allocate extra gen2 root space if needed. */
    if (tc->num_gen2roots == tc->alloc_gen2roots) {
        tc->alloc_gen2roots *= 2;
        tc->gen2roots = realloc(tc->gen2roots,
            sizeof(MVMCollectable **) * tc->alloc_gen2roots);
    }
    
    /* Add this one to the list. */
    tc->gen2roots[tc->num_gen2roots] = obj_ref;
    tc->num_gen2roots++;
}

/* Pushes an aggregate that is in generation 2, but now references a nursery
 * object, into the gen2 aggregates root set. */
void MVM_gc_root_gen2_agg_add(MVMThreadContext *tc, MVMObject *aggregate) {
    /* Ensure the root is not null. */
    if (aggregate == NULL)
        MVM_panic(MVM_exitcode_gcroots, "Illegal attempt to add null aggregate address as an inter-generational root");
    
    /* Allocate extra gen2 aggregate space if needed. */
    if (tc->num_gen2aggs == tc->alloc_gen2aggs) {
        tc->alloc_gen2aggs *= 2;
        tc->gen2aggs = realloc(tc->gen2aggs,
            sizeof(MVMObject *) * tc->alloc_gen2aggs);
    }
    
    /* Add this one to the list. */
    tc->gen2aggs[tc->num_gen2aggs] = aggregate;
    tc->num_gen2aggs++;
    
    /* Flag it as added, so we don't add it multiple times. */
    aggregate->header.flags |= MVM_CF_IN_GEN2_AGG_LIST;
}

/* Adds the set of thread-local inter-generational roots to a GC worklist. */
void MVM_gc_root_add_gen2s_to_worklist(MVMThreadContext *tc, MVMGCWorklist *worklist) {
    MVMuint32         i, num_roots, num_aggs;
    MVMCollectable ***gen2roots;
    MVMObject       **gen2aggs;
    
    /* Add objects rooted by something in gen2. */
    num_roots = tc->num_gen2roots;
    gen2roots = tc->gen2roots;
    for (i = 0; i < num_roots; i++)
        MVM_gc_worklist_add(tc, worklist, gen2roots[i]);
    
    /* Mark gen2 aggregates that point to gen1 things. */
    num_aggs = tc->num_gen2aggs;
    gen2aggs = tc->gen2aggs;
    for (i = 0; i < num_aggs; i++) {
        MVMObject *agg = gen2aggs[i];
        REPR(agg)->gc_mark(tc, STABLE(agg), OBJECT_BODY(agg), worklist);
        MVM_gc_worklist_add(tc, worklist, &gen2aggs[i]);
    }
}

/* Visits all of the roots in the gen2 list and cleans them up. */
void MVM_gc_root_gen2_cleanup_promoted(MVMThreadContext *tc) {
    MVMuint32         i, cur_survivor, num_roots;
    MVMCollectable ***gen2roots;
    num_roots = tc->num_gen2roots;
    gen2roots = tc->gen2roots;
    cur_survivor = 0;
    for (i = 0; i < num_roots; i++) {
        if (*gen2roots[i] && (*gen2roots[i])->forwarder && !((*gen2roots[i])->forwarder->flags & MVM_CF_SECOND_GEN)) {
            gen2roots[cur_survivor++] = gen2roots[i];
        }
    }
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
