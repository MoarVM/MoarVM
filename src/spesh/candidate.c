#include "moar.h"

/* Calculates the work and env sizes based on the number of locals and
 * lexicals. */
static void calculate_work_env_sizes(MVMThreadContext *tc, MVMStaticFrame *sf,
                                     MVMSpeshCandidate *c) {
    c->work_size = (c->num_locals + sf->body.cu->body.max_callsite_size)
        * sizeof(MVMRegister);
    c->env_size = c->num_lexicals * sizeof(MVMRegister);
}

/* Tries to set up a specialization of the bytecode for a given arg tuple.
 * Doesn't do the actual optimizations, just works out the guards and does
 * any simple argument transformations, and then inserts logging to record
 * what we see. Produces bytecode with those transformations and the logging
 * instructions. */
MVMSpeshCandidate * MVM_spesh_candidate_setup(MVMThreadContext *tc,
        MVMStaticFrame *static_frame, MVMCallsite *callsite, MVMRegister *args,
        MVMint32 osr) {
    MVMSpeshCandidate *result;
    MVMSpeshGuard *guards;
    MVMSpeshCode *sc;
    MVMint32 num_spesh_slots, num_log_slots, num_guards, *deopts, num_deopts;
    MVMuint16 num_locals, num_lexicals, used;
    MVMCollectable **spesh_slots, **log_slots;
    char *before, *after;
    MVMSpeshGraph *sg;

    /* If we're profiling, log we're starting spesh work. */
    if (tc->instance->profiling)
        MVM_profiler_log_spesh_start(tc);

    /* Do initial generation of the specialization, working out the argument
     * guards and adding logging. */
    sg = MVM_spesh_graph_create(tc, static_frame, 0);
    if (tc->instance->spesh_log_fh)
        before = MVM_spesh_dump(tc, sg);
    MVM_spesh_args(tc, sg, callsite, args);
    MVM_spesh_log_add_logging(tc, sg, osr);
    if (tc->instance->spesh_log_fh)
        after = MVM_spesh_dump(tc, sg);
    sc              = MVM_spesh_codegen(tc, sg);
    num_guards      = sg->num_arg_guards;
    guards          = sg->arg_guards;
    num_deopts      = sg->num_deopt_addrs;
    deopts          = sg->deopt_addrs;
    num_spesh_slots = sg->num_spesh_slots;
    spesh_slots     = sg->spesh_slots;
    num_log_slots   = sg->num_log_slots;
    log_slots       = sg->log_slots;
    num_locals      = sg->num_locals;
    num_lexicals    = sg->num_lexicals;

    /* Now try to add it. Note there's a slim chance another thread beat us
     * to doing so. Also other threads can read the specializations without
     * lock, so make absolutely sure we increment the count of them after we
     * add the new one. */
    result    = NULL;
    used      = 0;
    uv_mutex_lock(&tc->instance->mutex_spesh_install);
    if (static_frame->body.num_spesh_candidates < MVM_SPESH_LIMIT) {
        MVMint32 num_spesh = static_frame->body.num_spesh_candidates;
        MVMint32 i;
        for (i = 0; i < num_spesh; i++) {
            MVMSpeshCandidate *compare = &static_frame->body.spesh_candidates[i];
            if (compare->cs == callsite && compare->num_guards == num_guards &&
                memcmp(compare->guards, guards, num_guards * sizeof(MVMSpeshGuard)) == 0) {
                /* Beaten! */
                result = osr ? NULL : &static_frame->body.spesh_candidates[i];
                break;
            }
        }
        if (!result) {
            if (!static_frame->body.spesh_candidates)
                static_frame->body.spesh_candidates = MVM_calloc(
                    MVM_SPESH_LIMIT, sizeof(MVMSpeshCandidate));
            result                      = &static_frame->body.spesh_candidates[num_spesh];
            result->cs                  = callsite;
            result->num_guards          = num_guards;
            result->guards              = guards;
            result->bytecode            = sc->bytecode;
            result->bytecode_size       = sc->bytecode_size;
            result->handlers            = sc->handlers;
            result->num_handlers        = sg->num_handlers;
            result->num_spesh_slots     = num_spesh_slots;
            result->spesh_slots         = spesh_slots;
            result->num_deopts          = num_deopts;
            result->deopts              = deopts;
            result->num_log_slots       = num_log_slots;
            result->log_slots           = log_slots;
            result->num_locals          = num_locals;
            result->num_lexicals        = num_lexicals;
            result->local_types         = sg->local_types;
            result->lexical_types       = sg->lexical_types;
            result->sg                  = sg;
            result->log_enter_idx       = 0;
            result->log_exits_remaining = MVM_SPESH_LOG_RUNS;
            calculate_work_env_sizes(tc, static_frame, result);
            if (osr)
                result->osr_logging = 1;
            MVM_barrier();
            static_frame->body.num_spesh_candidates++;
            if (static_frame->common.header.flags & MVM_CF_SECOND_GEN)
                MVM_gc_write_barrier_hit(tc, (MVMCollectable *)static_frame);
            if (tc->instance->spesh_log_fh) {
                char *c_name = MVM_string_utf8_encode_C_string(tc, static_frame->body.name);
                char *c_cuid = MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid);
                fprintf(tc->instance->spesh_log_fh,
                    "Inserting logging for specialization of '%s' (cuid: %s)\n\n", c_name, c_cuid);
                fprintf(tc->instance->spesh_log_fh,
                    "Before:\n%s\nAfter:\n%s\n\n========\n\n", before, after);
                fflush(tc->instance->spesh_log_fh);
                MVM_free(before);
                MVM_free(after);
                MVM_free(c_name);
                MVM_free(c_cuid);
            }
            used = 1;
        }
    }
    if (result && !used) {
        MVM_free(sc->bytecode);
        if (sc->handlers)
            MVM_free(sc->handlers);
        MVM_spesh_graph_destroy(tc, sg);
    }
    uv_mutex_unlock(&tc->instance->mutex_spesh_install);

    /* If we're profiling, log we've finished spesh work. */
    if (tc->instance->profiling)
        MVM_profiler_log_spesh_end(tc);

    MVM_free(sc);
    return result;
}

/* Called at the point we have the finished logging for a specialization and
 * so are ready to do the specialization work for it. We can be sure this
 * will only be called once, and when nothing is running the logging version
 * of the code. */
void MVM_spesh_candidate_specialize(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMSpeshCandidate *candidate) {
    MVMSpeshCode  *sc;
    MVMSpeshGraph *sg;
    MVMJitGraph   *jg = NULL;

    /* If we're profiling, log we're starting spesh work. */
    if (tc->instance->profiling)
        MVM_profiler_log_spesh_start(tc);

    /* Obtain the graph, add facts, and do optimization work. */
    sg = candidate->sg;
    MVM_spesh_facts_discover(tc, sg);
    MVM_spesh_optimize(tc, sg);

    /* Dump updated graph if needed. */
    if (tc->instance->spesh_log_fh) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, static_frame->body.name);
        char *c_cuid = MVM_string_utf8_encode_C_string(tc, static_frame->body.cuuid);
        char *dump   = MVM_spesh_dump(tc, sg);
        fprintf(tc->instance->spesh_log_fh,
            "Finished specialization of '%s' (cuid: %s)\n\n", c_name, c_cuid);
        fprintf(tc->instance->spesh_log_fh,
            "%s\n\n========\n\n", dump);
        fflush(tc->instance->spesh_log_fh);
        MVM_free(dump);
        MVM_free(c_name);
        MVM_free(c_cuid);
    }

    /* Generate code, and replace that in the candidate. */
    sc = MVM_spesh_codegen(tc, sg);
    MVM_free(candidate->bytecode);
    if (candidate->handlers)
        MVM_free(candidate->handlers);
    candidate->bytecode      = sc->bytecode;
    candidate->bytecode_size = sc->bytecode_size;
    candidate->handlers      = sc->handlers;
    candidate->num_handlers  = sg->num_handlers;
    candidate->num_deopts    = sg->num_deopt_addrs;
    candidate->deopts        = sg->deopt_addrs;
    candidate->num_locals    = sg->num_locals;
    candidate->num_lexicals  = sg->num_lexicals;
    candidate->num_inlines   = sg->num_inlines;
    candidate->inlines       = sg->inlines;
    candidate->local_types   = sg->local_types;
    candidate->lexical_types = sg->lexical_types;
    calculate_work_env_sizes(tc, static_frame, candidate);
    MVM_free(sc);

    /* Try to JIT compile the optimised graph. The JIT graph hangs from
     * the spesh graph and can safely be deleted with it. */
    if (tc->instance->jit_enabled) {
        jg = MVM_jit_try_make_graph(tc, sg);
        if (jg != NULL) {
            candidate->jitcode = MVM_jit_compile_graph(tc, jg);
            MVM_jit_graph_destroy(tc, jg);
        }
    }

    /* Update spesh slots. */
    candidate->num_spesh_slots = sg->num_spesh_slots;
    candidate->spesh_slots     = sg->spesh_slots;

    /* May now be referencing nursery objects, so barrier just in case. */
    if (static_frame->common.header.flags & MVM_CF_SECOND_GEN)
        MVM_gc_write_barrier_hit(tc, (MVMCollectable *)static_frame);

    /* Destory spesh graph, and finally clear point to it in the candidate,
     * which unblocks use of the specialization. */
    if (candidate->num_inlines) {
        MVMint32 i;
        for (i = 0; i < candidate->num_inlines; i++)
            if (candidate->inlines[i].g) {
                MVM_spesh_graph_destroy(tc, candidate->inlines[i].g);
                candidate->inlines[i].g = NULL;
            }
    }
    MVM_spesh_graph_destroy(tc, sg);
    MVM_barrier();
    candidate->sg = NULL;

    /* If we're profiling, log we've finished spesh work. */
    if (tc->instance->profiling)
        MVM_profiler_log_spesh_end(tc);
}
