#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps SpeshCandidate_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &SpeshCandidate_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMSpeshCandidate);
    });

    return st->WHAT;
}

/* Initialize the representation. */
const MVMREPROps * MVMSpeshCandidate_initialize(MVMThreadContext *tc) {
    return &SpeshCandidate_this_repr;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMSpeshCandidateBody *body = (MVMSpeshCandidateBody *)data;

    MVMuint32 i;
    for (i = 0; i < body->num_spesh_slots; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->spesh_slots[i],
            "Spesh slot entry");
    for (i = 0; i < body->num_inlines; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->inlines[i].sf,
            "Spesh inlined static frame");
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation SpeshCandidate");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMSpeshCandidateBody *candidate = (MVMSpeshCandidateBody *)data;
    MVMuint32 i;
    if (candidate->type_tuple) {
        for (i = 0; i < candidate->cs->flag_count; i++) {
            MVM_gc_worklist_add(tc, worklist, &(candidate->type_tuple[i].type));
            MVM_gc_worklist_add(tc, worklist, &(candidate->type_tuple[i].decont_type));
        }
    }
    for (i = 0; i < candidate->num_spesh_slots; i++) {
        MVM_gc_worklist_add(tc, worklist, &(candidate->spesh_slots[i]));
    }
    for (i = 0; i < candidate->num_inlines; i++) {
        MVM_gc_worklist_add(tc, worklist, &(candidate->inlines[i].sf));
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSpeshCandidate *candidate = (MVMSpeshCandidate *)obj;
    MVM_free(candidate->body.type_tuple);
    MVM_free(candidate->body.bytecode);
    MVM_free(candidate->body.handlers);
    MVM_free(candidate->body.spesh_slots);
    MVM_free(candidate->body.deopts);
    MVM_spesh_pea_destroy_deopt_info(tc, &(candidate->body.deopt_pea));
    MVM_free(candidate->body.inlines);
    for (MVMuint32 i = 0; i < candidate->body.num_resume_inits; i++)
        MVM_free(candidate->body.resume_inits[i].init_registers);
    MVM_free(candidate->body.resume_inits);
    MVM_free(candidate->body.local_types);
    MVM_free(candidate->body.lexical_types);
    if (candidate->body.jitcode)
        MVM_jit_code_destroy(tc, candidate->body.jitcode);
    MVM_free(candidate->body.deopt_usage_info);
    MVM_free(candidate->body.deopt_synths);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMSpeshCandidate);
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMSpeshCandidateBody *body = (MVMSpeshCandidateBody *)data;
    MVMuint64 size = 0;

    size += body->bytecode_size;

    size += sizeof(MVMFrameHandler) * body->num_handlers;

    size += sizeof(MVMCollectable *) * body->num_spesh_slots;

    size += sizeof(MVMint32) * body->num_deopts;

    size += sizeof(MVMint32) * body->num_deopt_synths * 2; /* 2 values per entry */

    size += sizeof(MVMSpeshInline) * body->num_inlines;

    size += sizeof(MVMuint16) * (body->num_locals + body->num_lexicals);

    /* Dive into the jit code */
    if (body->jitcode) {
        MVMJitCode *code = body->jitcode;

        size += sizeof(MVMJitCode);

        size += sizeof(void *) * code->num_labels;

        size += sizeof(MVMJitDeopt) * code->num_deopts;
        size += sizeof(MVMJitInline) * code->num_inlines;
        size += sizeof(MVMJitHandler) * code->num_handlers;
        if (code->local_types)
            size += sizeof(MVMuint16) * code->num_locals;
    }

    return size;
}

static const MVMREPROps SpeshCandidate_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMSpeshCandidate", /* name */
    MVM_REPR_ID_MVMSpeshCandidate,
    unmanaged_size,
    NULL /* describe_refs */
};

/* Calculates the work and env sizes based on the number of locals and
 * lexicals. */
static void calculate_work_env_sizes(MVMThreadContext *tc, MVMStaticFrame *sf,
                                     MVMSpeshCandidate *c) {
    MVMuint32 jit_spill_size;

    jit_spill_size = (c->body.jitcode ? c->body.jitcode->spill_size: 0);

    c->body.work_size = (c->body.num_locals + jit_spill_size) * sizeof(MVMRegister);
    c->body.env_size = c->body.num_lexicals * sizeof(MVMRegister);
}

/* Called at points where we can GC safely during specialization. */
static void spesh_gc_point(MVMThreadContext *tc) {
#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif
    GC_SYNC_POINT(tc);
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
}

/* Produces and installs a specialized version of the code, according to the
 * specified plan. */
void MVM_spesh_candidate_add(MVMThreadContext *tc, MVMSpeshPlanned *p) {
    MVMSpeshGraph *sg;
    MVMSpeshCode *sc;
    MVMSpeshCandidate *candidate;
    MVMSpeshCandidate **new_candidate_list;
    MVMStaticFrameSpesh *spesh;
    MVMuint64 start_time = 0, spesh_time = 0, jit_time = 0, end_time;

    /* If we've reached our specialization limit, don't continue. */
    MVMint32 spesh_produced = ++tc->instance->spesh_produced;
    if (tc->instance->spesh_limit)
        if (spesh_produced > tc->instance->spesh_limit)
            return;

    /* Produce the specialization graph and, if we're logging, dump it out
     * pre-transformation. */
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
    sg = MVM_spesh_graph_create(tc, p->sf, 0, 1);
    if (MVM_spesh_debug_enabled(tc)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, p->sf->body.name);
        char *c_cuid = MVM_string_utf8_encode_C_string(tc, p->sf->body.cuuid);
        MVMSpeshFacts **facts = sg->facts;
        char *before;
        sg->facts = NULL;
        before = MVM_spesh_dump(tc, sg);
        sg->facts = facts;
        MVM_spesh_debug_printf(tc,
            "Specialization of '%s' (cuid: %s)\n\n", c_name, c_cuid);
        MVM_spesh_debug_printf(tc, "Before:\n%s", before);
        MVM_free(c_name);
        MVM_free(c_cuid);
        MVM_free(before);
        fflush(tc->instance->spesh_log_fh);
        start_time = uv_hrtime();
    }

    /* Attach the graph so we will be able to mark it during optimization,
     * allowing us to stick GC sync points at various places and so not let
     * the optimization work block GC for too long. */
    tc->spesh_active_graph = sg;
    spesh_gc_point(tc);

    /* Perform the optimization and, if we're logging, dump out the result. */
    if (p->cs_stats->cs)
        MVM_spesh_args(tc, sg, p->cs_stats->cs, p->type_tuple);
    spesh_gc_point(tc);
    MVM_spesh_facts_discover(tc, sg, p, 0);
    spesh_gc_point(tc);
    MVM_spesh_optimize(tc, sg, p);
    spesh_gc_point(tc);

    if (MVM_spesh_debug_enabled(tc))
        spesh_time = uv_hrtime();

    /* Generate code and install it into the candidate. */
    sc = MVM_spesh_codegen(tc, sg);

#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif

    /* Allocate the candidate. If we end up with it in gen2, then make sure it
     * goes in the remembered set incase it ends up pointing to nursery objects. */
    candidate = (MVMSpeshCandidate *)MVM_repr_alloc_init(tc, tc->instance->SpeshCandidate);
    if (candidate->common.header.flags2 & MVM_CF_SECOND_GEN)
        MVM_gc_write_barrier_hit(tc, (MVMCollectable *)candidate);

    /* Clear active graph; beyond this point, no more GC syncs. */
    tc->spesh_active_graph = NULL;

#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif

    candidate->body.cs            = p->cs_stats->cs;
    candidate->body.type_tuple    = p->type_tuple
        ? MVM_spesh_plan_copy_type_tuple(tc, candidate->body.cs, p->type_tuple)
        : NULL;
    candidate->body.bytecode      = sc->bytecode;
    candidate->body.bytecode_size = sc->bytecode_size;
    candidate->body.handlers      = sc->handlers;
    candidate->body.deopt_usage_info = sc->deopt_usage_info;
    candidate->body.deopt_synths  = sc->deopt_synths;
    candidate->body.num_deopt_synths = sc->num_deopt_synths;
    candidate->body.num_handlers  = sg->num_handlers;
    candidate->body.num_deopts    = sg->num_deopt_addrs;
    candidate->body.deopts        = sg->deopt_addrs;
    candidate->body.num_resume_inits = MVM_VECTOR_ELEMS(sg->resume_inits);
    candidate->body.resume_inits = sg->resume_inits;
    candidate->body.deopt_named_used_bit_field = sg->deopt_named_used_bit_field;
    candidate->body.deopt_pea     = sg->deopt_pea;
    candidate->body.num_locals    = sg->num_locals;
    candidate->body.num_lexicals  = sg->num_lexicals;
    candidate->body.num_inlines   = sg->num_inlines;
    candidate->body.inlines       = sg->inlines;
    candidate->body.local_types   = sg->local_types;
    candidate->body.lexical_types = sg->lexical_types;

    MVM_free(sc);

    /* Try to JIT compile the optimised graph. The JIT graph hangs from
     * the spesh graph and can safely be deleted with it. */
    if (tc->instance->jit_enabled) {
        MVMJitGraph *jg;
        if (MVM_spesh_debug_enabled(tc))
            jit_time = uv_hrtime();

        jg = MVM_jit_try_make_graph(tc, sg);
        if (jg != NULL) {
            candidate->body.jitcode = MVM_jit_compile_graph(tc, jg);
            MVM_jit_graph_destroy(tc, jg);
        }
    }

    if (MVM_spesh_debug_enabled(tc)) {
        char *after = MVM_spesh_dump(tc, sg);
        end_time = uv_hrtime();
        MVM_spesh_debug_printf(tc, "After:\n%s", after);
        MVM_spesh_debug_printf(tc,
            "Specialization took %" PRIu64 "us (total %" PRIu64"us)\n",
            (spesh_time - start_time) / 1000,
            (end_time - start_time) / 1000);

        if (tc->instance->jit_enabled) {
            MVM_spesh_debug_printf(tc,
                "JIT was %ssuccessful and compilation took %" PRIu64 "us\n",
                candidate->body.jitcode ? "" : "not ", (end_time - jit_time) / 1000);
            if (candidate->body.jitcode) {
                MVM_spesh_debug_printf(tc, "    Bytecode size: %" PRIu64 " byte\n",
                                       candidate->body.jitcode->size);
            }
        }
        MVM_spesh_debug_printf(tc, "\n========\n\n");
        MVM_free(after);
        fflush(tc->instance->spesh_log_fh);
    }

    /* Calculate work environment taking JIT spill area into account. */
    calculate_work_env_sizes(tc, sg->sf, candidate);

    /* Update spesh slots. */
    candidate->body.num_spesh_slots = sg->num_spesh_slots;
    candidate->body.spesh_slots     = sg->spesh_slots;

    /* Claim ownership of allocated memory assigned to the candidate. */
    sg->cand = candidate;
    MVM_spesh_graph_destroy(tc, sg);

    /* Create a new candidate list and copy any existing ones. Free memory
     * using the safepoint mechanism. */
    spesh = p->sf->body.spesh;
    new_candidate_list = MVM_malloc((spesh->body.num_spesh_candidates + 1) * sizeof(MVMSpeshCandidate *));
    if (spesh->body.num_spesh_candidates) {
        size_t orig_size = spesh->body.num_spesh_candidates * sizeof(MVMSpeshCandidate *);
        memcpy(new_candidate_list, spesh->body.spesh_candidates, orig_size);
        MVM_free_at_safepoint(tc, spesh->body.spesh_candidates);
    }
    MVM_ASSIGN_REF(tc, &(spesh->common.header), new_candidate_list[spesh->body.num_spesh_candidates], candidate);
    spesh->body.spesh_candidates = new_candidate_list;

    /* Regenerate the guards, and bump the candidate count only after they
     * are installed. This means there is a period when we can read, in
     * another thread, a candidate ahead of the count being updated. Since
     * we set it up above, that's fine enough. The updating of the count
     * *after* this, plus the barrier, is to make sure the guards are in
     * place before the count is bumped, since OSR will watch the number
     * of candidates to see if there's one for it to try and jump in to,
     * and if the guards aren't in place first will see there is not, and
     * not bother checking again. */
    MVM_spesh_arg_guard_regenerate(tc, &(spesh->body.spesh_arg_guard),
        spesh->body.spesh_candidates, spesh->body.num_spesh_candidates + 1);
    if (spesh->common.header.flags2 & MVM_CF_SECOND_GEN)
        MVM_gc_write_barrier_hit(tc, (MVMCollectable *)spesh);
    MVM_barrier();
    spesh->body.num_spesh_candidates++;

    /* If we're logging, dump the updated arg guards also. */
    if (MVM_spesh_debug_enabled(tc)) {
        char *guard_dump = MVM_spesh_dump_arg_guard(tc, p->sf,
                p->sf->body.spesh->body.spesh_arg_guard);
        MVM_spesh_debug_printf(tc, "%s========\n\n", guard_dump);
        fflush(tc->instance->spesh_log_fh);
        MVM_free(guard_dump);
    }

#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif
}

/* Discards existing candidates. Used when we instrument bytecode, and so
 * need to ignore these ones from here on. */
void MVM_spesh_candidate_discard_existing(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMStaticFrameSpesh *spesh = sf->body.spesh;
    if (spesh) {
        MVMuint32 num_candidates = spesh->body.num_spesh_candidates;
        MVMuint32 i;
        for (i = 0; i < num_candidates; i++)
            spesh->body.spesh_candidates[i]->body.discarded = 1;
        MVM_spesh_arg_guard_discard(tc, sf);
    }
}
