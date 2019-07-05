#include "moar.h"

/* Calculates the work and env sizes based on the number of locals and
 * lexicals. */
static void calculate_work_env_sizes(MVMThreadContext *tc, MVMStaticFrame *sf,
                                     MVMSpeshCandidate *c) {
    MVMuint32 max_callsite_size, jit_spill_size;
    MVMint32 i;

    max_callsite_size = sf->body.cu->body.max_callsite_size;
    jit_spill_size = (c->jitcode ? c->jitcode->spill_size: 0);
    for (i = 0; i < c->num_inlines; i++) {
        MVMuint32 cs = c->inlines[i].sf->body.cu->body.max_callsite_size;
        if (cs > max_callsite_size)
            max_callsite_size = cs;
    }

    c->work_size = (c->num_locals + max_callsite_size + jit_spill_size) * sizeof(MVMRegister);
    c->env_size = c->num_lexicals * sizeof(MVMRegister);
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
    MVMuint64 start_time, spesh_time, jit_time, end_time;

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

    /* Clear active graph; beyond this point, no more GC syncs. */
    tc->spesh_active_graph = NULL;

    if (MVM_spesh_debug_enabled(tc))
        spesh_time = uv_hrtime();

    /* Generate code and install it into the candidate. */
    sc = MVM_spesh_codegen(tc, sg);
    candidate = MVM_calloc(1, sizeof(MVMSpeshCandidate));
    candidate->bytecode      = sc->bytecode;
    candidate->bytecode_size = sc->bytecode_size;
    candidate->handlers      = sc->handlers;
    candidate->deopt_usage_info = sc->deopt_usage_info;
    candidate->needs_arg_proc_context = sc->needs_arg_proc_context;
    candidate->num_handlers  = sg->num_handlers;
    candidate->num_deopts    = sg->num_deopt_addrs;
    candidate->deopts        = sg->deopt_addrs;
    candidate->deopt_named_used_bit_field = sg->deopt_named_used_bit_field;
    candidate->deopt_pea     = sg->deopt_pea;
    candidate->num_locals    = sg->num_locals;
    candidate->num_lexicals  = sg->num_lexicals;
    candidate->num_inlines   = sg->num_inlines;
    candidate->inlines       = sg->inlines;
    candidate->local_types   = sg->local_types;
    candidate->lexical_types = sg->lexical_types;

    MVM_free(sc);

    /* Try to JIT compile the optimised graph. The JIT graph hangs from
     * the spesh graph and can safely be deleted with it. */
    if (tc->instance->jit_enabled) {
        MVMJitGraph *jg;
        if (MVM_spesh_debug_enabled(tc))
            jit_time = uv_hrtime();

        jg = MVM_jit_try_make_graph(tc, sg);
        if (jg != NULL) {
            candidate->jitcode = MVM_jit_compile_graph(tc, jg);
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
                candidate->jitcode ? "" : "not ", (end_time - jit_time) / 1000);
            if (candidate->jitcode) {
                MVM_spesh_debug_printf(tc, "    Bytecode size: %" PRIu64 " byte\n",
                                       candidate->jitcode->size);
            }
        }
        MVM_spesh_debug_printf(tc, "\n========\n\n");
        MVM_free(after);
        fflush(tc->instance->spesh_log_fh);
    }

    /* calculate work environment taking JIT spill area into account */
    calculate_work_env_sizes(tc, sg->sf, candidate);

    /* Update spesh slots. */
    candidate->num_spesh_slots = sg->num_spesh_slots;
    candidate->spesh_slots     = sg->spesh_slots;

    /* Claim ownership of allocated memory assigned to the candidate */
    sg->cand = candidate;
    MVM_spesh_graph_destroy(tc, sg);

    /* Create a new candidate list and copy any existing ones. Free memory
     * using the FSA safepoint mechanism. */
    spesh = p->sf->body.spesh;
    new_candidate_list = MVM_fixed_size_alloc(tc, tc->instance->fsa,
        (spesh->body.num_spesh_candidates + 1) * sizeof(MVMSpeshCandidate *));
    if (spesh->body.num_spesh_candidates) {
        size_t orig_size = spesh->body.num_spesh_candidates * sizeof(MVMSpeshCandidate *);
        memcpy(new_candidate_list, spesh->body.spesh_candidates, orig_size);
        MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, orig_size,
            spesh->body.spesh_candidates);
    }
    new_candidate_list[spesh->body.num_spesh_candidates] = candidate;
    spesh->body.spesh_candidates = new_candidate_list;

    /* May now be referencing nursery objects, so barrier just in case. */
    if (spesh->common.header.flags & MVM_CF_SECOND_GEN)
        MVM_gc_write_barrier_hit(tc, (MVMCollectable *)spesh);

    /* Update the guards, and bump the candidate count. This means there is a
     * period when we can read, in another thread, a candidate ahead of the
     * count being updated. Since we set it up above, that's fine enough. The
     * updating of the count *after* this, plus the barrier, is to make sure
     * the guards are in place before the count is bumped, since OSR will
     * watch the number of candidates to see if there's one for it to try and
     * jump in to, and if the guards aren't in place first will see there is
     * not, and not bother checking again. */
    MVM_spesh_arg_guard_add(tc, &(spesh->body.spesh_arg_guard),
        p->cs_stats->cs, p->type_tuple, spesh->body.num_spesh_candidates);
    MVM_barrier();
    spesh->body.num_spesh_candidates++;

    /* If we're logging, dump the upadated arg guards also. */
    if (MVM_spesh_debug_enabled(tc)) {
        char *guard_dump = MVM_spesh_dump_arg_guard(tc, p->sf);
        MVM_spesh_debug_printf(tc, "%s========\n\n", guard_dump);
        fflush(tc->instance->spesh_log_fh);
        MVM_free(guard_dump);
    }

#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif
}

/* Frees the memory associated with a spesh candidate. */
void MVM_spesh_candidate_destroy(MVMThreadContext *tc, MVMSpeshCandidate *candidate) {
    MVM_free(candidate->bytecode);
    MVM_free(candidate->handlers);
    MVM_free(candidate->spesh_slots);
    MVM_free(candidate->deopts);
    MVM_spesh_pea_destroy_deopt_info(tc, &(candidate->deopt_pea));
    MVM_free(candidate->inlines);
    MVM_free(candidate->local_types);
    MVM_free(candidate->lexical_types);
    if (candidate->jitcode)
        MVM_jit_code_destroy(tc, candidate->jitcode);
    MVM_free(candidate->deopt_usage_info);
    MVM_free(candidate);
}
