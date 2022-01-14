#include "moar.h"

/* Checks if we have any existing specialization of this. */
static MVMint32 have_existing_specialization(MVMThreadContext *tc, MVMStaticFrame *sf,
        MVMCallsite *cs, MVMSpeshStatsType *type_tuple) {
    MVMStaticFrameSpesh *sfs = sf->body.spesh;
    MVMuint32 i;
    for (i = 0; i < sfs->body.num_spesh_candidates; i++) {
        if (sfs->body.spesh_candidates[i]->body.cs == cs) {
            /* Callsite matches. Is it a matching certain specialization? */
            MVMSpeshStatsType *cand_type_tuple = sfs->body.spesh_candidates[i]->body.type_tuple;
            if (type_tuple == NULL && cand_type_tuple == NULL) {
                /* Yes, so we're done. */
                return 1;
            }
            else if (type_tuple != NULL && cand_type_tuple != NULL) {
                /* Typed specialization, so compare the tuples. */
                size_t tt_size = cs->flag_count * sizeof(MVMSpeshStatsType);
                if (memcmp(type_tuple, cand_type_tuple, tt_size) == 0)
                    return 1;
            }
        }
    }
    return 0;
}

/* Adds a planned specialization, provided it doesn't already exist (this may
 * happen due to further data suggesting it being logged while it was being
 * produced). */
static void add_planned(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMSpeshPlannedKind kind,
                        MVMStaticFrame *sf, MVMSpeshStatsByCallsite *cs_stats,
                        MVMSpeshStatsType *type_tuple, MVMSpeshStatsByType **type_stats,
                        MVMuint32 num_type_stats) {
    MVMSpeshPlanned *p;
    if (sf->body.bytecode_size > MVM_SPESH_MAX_BYTECODE_SIZE ||
        have_existing_specialization(tc, sf, cs_stats->cs, type_tuple)) {
        /* Clean up allocated memory.
         * NB - the only caller is plan_for_cs, which means that we could do the
         * allocations in here, except that we need the type tuple for the
         * lookup already. So this is messy but it works. */
        MVM_free(type_stats);
        MVM_free(type_tuple);
        return;
    }
    if (plan->num_planned == plan->alloc_planned) {
        plan->alloc_planned += 16;
        plan->planned = MVM_realloc(plan->planned,
            plan->alloc_planned * sizeof(MVMSpeshPlanned));
    }
    p = &(plan->planned[plan->num_planned++]);
    p->kind = kind;
    p->sf = sf;
    p->cs_stats = cs_stats;
    p->type_tuple = type_tuple;
    p->type_stats = type_stats;
    p->num_type_stats = num_type_stats;
    if (num_type_stats) {
        MVMuint32 i;
        p->max_depth = type_stats[0]->max_depth;
        for (i = 1; i < num_type_stats; i++)
            if (type_stats[i]->max_depth > p->max_depth)
                p->max_depth = type_stats[i]->max_depth;
    }
    else {
        p->max_depth = cs_stats->max_depth;
    }
}

/* Makes a copy of an argument type tuple. */
MVMSpeshStatsType * MVM_spesh_plan_copy_type_tuple(MVMThreadContext *tc,
        MVMCallsite *cs, MVMSpeshStatsType *to_copy) {
    size_t stats_size = cs->flag_count * sizeof(MVMSpeshStatsType);
    MVMSpeshStatsType *result = MVM_malloc(stats_size);
    memcpy(result, to_copy, stats_size);
    return result;
}

/* Used to track counts of types in a given parameter position, for the purpose
 * of deciding what to specialize on. */
typedef struct {
    MVMSpeshStatsType type;
    MVMuint32 count;
} ParamTypeCount;

// TODO: rename, move
#define PERCENT_RELEVANT 40

/* Considers the statistics of a given callsite + static frame pairing and
 * plans specializations to produce for it. */
static void plan_for_cs(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf,
                        MVMSpeshStatsByCallsite *by_cs,
                        MVMuint64 *in_certain_specialization, MVMuint64 *in_observed_specialization,
                        MVMuint64 *in_osr_specialization) {
    /* First, make sure it even is possible to specialize something by type
     * in this code. */
    MVMuint32 specializations = 0;
    if (sf->body.specializable && by_cs->cs) {
        /* It is. We'll try and produce some specializations, looping until
         * no tuples that remain give us anything significant. */
        MVMuint32 required_hits = (PERCENT_RELEVANT * (by_cs->hits + by_cs->osr_hits)) / 100;
        MVMuint8 *tuples_used = MVM_calloc(by_cs->num_by_type, 1);
        MVMuint32 num_obj_args = 0, i;
        for (i = 0; i < by_cs->cs->flag_count; i++)
            if (by_cs->cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ)
                num_obj_args++;
        while (specializations < by_cs->num_by_type) {
            /* Here, we'll look through the incoming argument tuples to try
             * to produce a tuple to specialize on. In some cases, we'll find a
             * high degree of stability in all arguments, e.g. the Int candidate
             * of infix:<+> will probably just always have (Int,Int). In others,
             * we'll find only some arguments are stable, e.g. `push` is likely in
             * any non-trivial program to have little variance in the first
             * argument, but a lot in the second. */
            MVMSpeshStatsType *chosen_tuple = MVM_calloc(by_cs->cs->flag_count,
                    sizeof(MVMSpeshStatsType));
            MVMuint8 *chosen_position = MVM_calloc(by_cs->cs->flag_count, 1);
            MVMuint32 param_idx, j, k, have_chosen;
            MVM_VECTOR_DECL(ParamTypeCount, type_counts);
            MVM_VECTOR_INIT(type_counts, by_cs->num_by_type);
            for (param_idx = 0; param_idx < by_cs->cs->flag_count; param_idx++) {
                /* Skip over non-object types. */
                if (!(by_cs->cs->arg_flags[param_idx] & MVM_CALLSITE_ARG_OBJ))
                    continue;

                /* Sum up the counts of this parameter's types. */
                MVM_VECTOR_CLEAR(type_counts);
                for (j = 0; j < by_cs->num_by_type; j++) {
                    /* Make sure that the prefix matches what we've already decided
                     * to focus on, and that the tuple wasn't already covered. */
                    MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                    MVMint32 found, valid;
                    if (tuples_used[j])
                        continue;
                    valid = 1;
                    for (k = 0; k < param_idx; k++) {
                        if (chosen_position[k] && memcmp(&(by_type->arg_types[k]),
                                    &(chosen_tuple[k]), sizeof(MVMSpeshStatsType)) != 0) {
                            valid = 0;
                            break;
                        }
                    }
                    if (!valid)
                        continue;

                    /* Otherwise, assemble its counts. */
                    found = 0;
                    for (k = 0; k < MVM_VECTOR_ELEMS(type_counts); k++) {
                        if (memcmp(&(type_counts[k].type), &(by_type->arg_types[param_idx]),
                                    sizeof(MVMSpeshStatsType)) == 0) {
                            type_counts[k].count += by_type->hits + by_type->osr_hits;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        ParamTypeCount ptc;
                        ptc.type = by_type->arg_types[param_idx];
                        ptc.count = by_type->hits + by_type->osr_hits;
                        MVM_VECTOR_PUSH(type_counts, ptc);
                    }
                }

                /* Go through those hits and find an entry that meets the
                 * threshold if we are to specialize on it. */
                for (j = 0; j < MVM_VECTOR_ELEMS(type_counts); j++) {
                    if (type_counts[j].count >= required_hits) {
                        /* Yes, found; copy into the type tuple and mark chosen. */
                        chosen_tuple[param_idx] = type_counts[j].type;
                        chosen_position[param_idx] = 1;
                        break;
                    }
                }
            }
            MVM_VECTOR_DESTROY(type_counts);

            /* By this point, we may well have a tuple to specialize on. Check
             * if that is the case. */
            have_chosen = 0;
            for (j = 0; j < by_cs->cs->flag_count; j++) {
                if (chosen_position[j]) {
                    have_chosen = 1;
                    break;
                }
            }
            if (have_chosen || num_obj_args == 0) {
                /* Yes, we have a decision. Gather all tuples that provide
                 * evidence for the choice. */
                MVM_VECTOR_DECL(MVMSpeshStatsByType *, evidence);
                MVM_VECTOR_INIT(evidence, 4);
                for (j = 0; j < by_cs->num_by_type; j++) {
                    MVMint32 matching = 1;
                    for (k = 0; k < by_cs->cs->flag_count; k++) {
                        if (chosen_position[k] && memcmp(&(by_cs->by_type[j].arg_types[k]),
                                    &(chosen_tuple[k]), sizeof(MVMSpeshStatsType)) != 0) {
                            matching = 0;
                            break;
                        }
                    }
                    if (matching) {
                        MVM_VECTOR_PUSH(evidence, &(by_cs->by_type[j]));
                        tuples_used[j] = 1;
                    }
                }

                /* Add the specialization. */
                add_planned(tc, plan,
                    MVM_VECTOR_ELEMS(evidence) == 1
                        ? MVM_SPESH_PLANNED_OBSERVED_TYPES
                        : MVM_SPESH_PLANNED_DERIVED_TYPES,
                    sf, by_cs, chosen_tuple, evidence, MVM_VECTOR_ELEMS(evidence));
                specializations++;

                /* Clean up and we're done. */
                MVM_free(chosen_position);
            }
            else {
                /* No fitting tuple; clean up and leave the loop. */
                MVM_free(chosen_position);
                MVM_free(chosen_tuple);
                break;
            }
        }

        /* Clean up allocated memory. */
        MVM_free(tuples_used);
    }

    /* If we get here, and found no specializations to produce, we can add
     * a certain specializaiton instead. */
    if (!specializations)
        add_planned(tc, plan, MVM_SPESH_PLANNED_CERTAIN, sf, by_cs, NULL, NULL, 0);
}

/* Considers the statistics of a given static frame and plans specializtions
 * to produce for it. */
static void plan_for_sf(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf,
        MVMuint64 *in_certain_specialization, MVMuint64 *in_observed_specialization, MVMuint64 *in_osr_specialization) {
    MVMSpeshStats *ss = sf->body.spesh->body.spesh_stats;
    MVMuint32 threshold = MVM_spesh_threshold(tc, sf);
    if (ss->hits >= threshold || ss->osr_hits >= MVM_SPESH_PLAN_SF_MIN_OSR) {
        /* The frame is hot enough; look through its callsites to see if any
         * of those are. */
        MVMuint32 i;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            if (by_cs->hits >= threshold || by_cs->osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
                plan_for_cs(tc, plan, sf, by_cs, in_certain_specialization, in_observed_specialization, in_osr_specialization);
        }
    }
}

/* Maximum stack depth is a decent heuristic for the order to specialize in,
 * but sometimes it's misleading, and we end up with a planned specialization
 * of a callee having a lower maximum than the caller. Boost the depth of any
 * callees in such a situation. */
static void twiddle_stack_depths(MVMThreadContext *tc, MVMSpeshPlanned *planned, MVMuint32 num_planned) {
    MVMuint32 i;
    if (num_planned < 2)
        return;
    for (i = 0; i < num_planned; i++) {
        /* For each planned specialization, look for its calls. */
        MVMSpeshPlanned *p = &(planned[i]);
        MVMuint32 j;
        for (j = 0; j < p->num_type_stats; j++) {
            MVMSpeshStatsByType *sbt = p->type_stats[j];
            MVMuint32 k;
            for (k = 0; k < sbt->num_by_offset; k++) {
                MVMSpeshStatsByOffset *sbo = &(sbt->by_offset[k]);
                MVMuint32 l;
                for (l = 0; l < sbo->num_invokes; l++) {
                    /* Found an invoke. If we plan a specialization for it,
                     * then bump its count. */
                    MVMStaticFrame *invoked_sf = sbo->invokes[l].sf;
                    MVMuint32 m;
                    for (m = 0; m < num_planned; m++)
                        if (planned[m].sf == invoked_sf)
                            planned[m].max_depth = p->max_depth + 1;
                }
            }
        }
    }
}

/* Sorts the plan in descending order of maximum call depth. */
static void sort_plan(MVMThreadContext *tc, MVMSpeshPlanned *planned, MVMuint32 n) {
    if (n >= 2) {
        MVMSpeshPlanned pivot = planned[n / 2];
        MVMuint32 i, j;
        for (i = 0, j = n - 1; ; i++, j--) {
            MVMSpeshPlanned temp;
            while (planned[i].max_depth > pivot.max_depth)
                i++;
            while (planned[j].max_depth < pivot.max_depth)
                j--;
            if (i >= j)
                break;
            temp = planned[i];
            planned[i] = planned[j];
            planned[j] = temp;
        }
        sort_plan(tc, planned, i);
        sort_plan(tc, planned + i, n - i);
    }
}

/* Forms a specialization plan from considering all frames whose statics have
 * changed. */
MVMSpeshPlan * MVM_spesh_plan(MVMThreadContext *tc, MVMObject *updated_static_frames, MVMuint64 *in_certain_specialization, MVMuint64 *in_observed_specialization, MVMuint64 *in_osr_specialization) {
    MVMSpeshPlan *plan = MVM_calloc(1, sizeof(MVMSpeshPlan));
    MVMint64 updated = MVM_repr_elems(tc, updated_static_frames);
    MVMint64 i;
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
    for (i = 0; i < updated; i++) {
        MVMObject *sf = MVM_repr_at_pos_o(tc, updated_static_frames, i);
        plan_for_sf(tc, plan, (MVMStaticFrame *)sf, in_certain_specialization, in_observed_specialization, in_osr_specialization);
    }
    twiddle_stack_depths(tc, plan->planned, plan->num_planned);
    sort_plan(tc, plan->planned, plan->num_planned);
#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif
    return plan;
}

/* Marks garbage-collectable objects held in the spesh plan. */
void MVM_spesh_plan_gc_mark(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMGCWorklist *worklist) {
    MVMuint32 i;
    if (!plan)
        return;
    for (i = 0; i < plan->num_planned; i++) {
        MVMSpeshPlanned *p = &(plan->planned[i]);
        MVM_gc_worklist_add(tc, worklist, &(p->sf));
        if (p->type_tuple) {
            MVMCallsite *cs = p->cs_stats->cs;
            MVMuint32 j;
            for (j = 0; j < cs->flag_count; j++) {
                if (cs->arg_flags[j] & MVM_CALLSITE_ARG_OBJ) {
                    MVM_gc_worklist_add(tc, worklist, &(p->type_tuple[j].type));
                    MVM_gc_worklist_add(tc, worklist, &(p->type_tuple[j].decont_type));
                }
            }
        }
    }
}

void MVM_spesh_plan_gc_describe(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSpeshPlan *plan) {
    MVMuint32 i;
    MVMuint64 cache_1 = 0;
    MVMuint64 cache_2 = 0;
    MVMuint64 cache_3 = 0;
    if (!plan)
        return;
    for (i = 0; i < plan->num_planned; i++) {
        MVMSpeshPlanned *p = &(plan->planned[i]);
        MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
            (MVMCollectable*)(p->sf), "staticframe", &cache_1);
        if (p->type_tuple) {
            MVMCallsite *cs = p->cs_stats->cs;
            MVMuint32 j;
            for (j = 0; j < cs->flag_count; j++) {
                if (cs->arg_flags[j] & MVM_CALLSITE_ARG_OBJ) {
                    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                        (MVMCollectable*)(p->type_tuple[j].type), "argument type", &cache_2);
                    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                        (MVMCollectable*)(p->type_tuple[j].decont_type), "argument decont type", &cache_3);
                }
            }
        }
    }
}

/* Frees all memory associated with a specialization plan. */
void MVM_spesh_plan_destroy(MVMThreadContext *tc, MVMSpeshPlan *plan) {
    MVMuint32 i;
    for (i = 0; i < plan->num_planned; i++) {
        MVM_free(plan->planned[i].type_stats);
        MVM_free(plan->planned[i].type_tuple);
    }
    MVM_free(plan->planned);
    MVM_free(plan);
}
