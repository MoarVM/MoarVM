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
    MVMSerializationContextBody *current, *tmp;

    MVM_gc_worklist_add(tc, worklist, &tc->instance->threads);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->compiler_registry);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->hll_syms);
    MVM_gc_worklist_add(tc, worklist, &tc->instance->clargs);

    /* okay, so this makes the weak hash slightly less weak.. for certain
     * keys of it anyway... */
    HASH_ITER(hash_handle, tc->instance->sc_weakhash, current, tmp) {
        /* mark the string handle pointer iff it hasn't yet been resolved */
        if (!current->sc)
            MVM_gc_worklist_add(tc, worklist, &current->handle);
    }
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
    
    /* List of SCs currently being compiled. */
    MVM_gc_worklist_add(tc, worklist, &tc->compiling_scs);
    
    /* compunit variable pointer */
    MVM_gc_worklist_add(tc, worklist, tc->interp_cu);
    
    /* its current frame */
    MVM_gc_worklist_add_frame(tc, worklist, tc->cur_frame);
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
    MVM_gc_worklist_presize_for(tc, worklist, num_roots);
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
    MVMFrame *cur_frame = start_frame;
    MVMuint32 cur_seq_number = tc->instance->gc_seq_number;
    /* If we already saw the frame this run, skip it. */
    MVMuint32 orig_seq = cur_frame->gc_seq_number;
    if (orig_seq == cur_seq_number)
        return;
    if (apr_atomic_cas32(&cur_frame->gc_seq_number, cur_seq_number, orig_seq) != orig_seq)
        return;

    /* Add caller and outer to frames work list. */
    MVM_gc_worklist_add_frame(tc, worklist, cur_frame->caller);
    MVM_gc_worklist_add_frame(tc, worklist, cur_frame->outer);

    /* add code_ref to work list unless we're the top-level frame. */
    if (cur_frame->code_ref)
        MVM_gc_worklist_add(tc, worklist, &cur_frame->code_ref);
    MVM_gc_worklist_add(tc, worklist, &cur_frame->static_info);

    /* Scan the registers. */
    scan_registers(tc, worklist, cur_frame);
}

/* Takes a frame, scans its registers and adds them to the roots. */
static void scan_registers(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMFrame *frame) {
    MVMuint16  i, count;
    MVMuint16 *type_map;
    MVMuint8  *flag_map;

    /* Scan locals. */
    if (frame->work && frame->tc) {
        type_map = frame->static_info->body.local_types;
        count    = frame->static_info->body.num_locals;
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &frame->work[i].o);
    }

    /* Scan lexicals. */
    if (frame->env) {
        type_map = frame->static_info->body.lexical_types;
        count    = frame->static_info->body.num_lexicals;
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
