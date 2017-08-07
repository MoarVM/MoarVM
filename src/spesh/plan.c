#include "moar.h"

/* Adds a planned specialization, provided it doesn't already exist (this may
 * happen due to further data suggesting it being logged while it was being
 * produced). */
void add_planned(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMSpeshPlannedKind kind,
                 MVMStaticFrame *sf, MVMSpeshStatsByCallsite *cs_stats,
                 MVMSpeshStatsType *type_tuple, MVMSpeshStatsByType **type_stats,
                 MVMuint32 num_type_stats) {
    MVMSpeshPlanned *p;
    if (sf->body.bytecode_size > MVM_SPESH_MAX_BYTECODE_SIZE)
        return;
    if (MVM_spesh_arg_guard_exists(tc, sf->body.spesh->body.spesh_arg_guard, cs_stats->cs, type_tuple))
        return;
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
MVMSpeshStatsType * copy_type_tuple(MVMThreadContext *tc, MVMCallsite *cs,
        MVMSpeshStatsType *to_copy) {
    size_t stats_size = cs->flag_count * sizeof(MVMSpeshStatsType);
    MVMSpeshStatsType *result = MVM_malloc(stats_size);
    memcpy(result, to_copy, stats_size);
    return result;
}

/* Considers the statistics of a given callsite + static frame pairing and
 * plans specializations to produce for it. */
void plan_for_cs(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf,
                 MVMSpeshStatsByCallsite *by_cs) {
    /* See if any types tuples are hot enough. */
    MVMuint32 i;
    MVMuint32 unaccounted_hits = by_cs->hits;
    MVMuint32 unaccounted_osr_hits = by_cs->osr_hits;
    for (i = 0; i < by_cs->num_by_type; i++) {
        MVMSpeshStatsByType *by_type = &(by_cs->by_type[i]);
        MVMuint32 hit_percent = by_cs->hits
           ? (100 * by_type->hits) / by_cs->hits
           : 0;
        MVMuint32 osr_hit_percent = by_cs->osr_hits
            ? (100 * by_type->osr_hits) / by_cs->osr_hits
            : 0;
        if (by_cs->cs && (hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT ||
                osr_hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT_OSR)) {
            MVMSpeshStatsByType **evidence = MVM_malloc(sizeof(MVMSpeshStatsByType *));
            evidence[0] = by_type;
            add_planned(tc, plan, MVM_SPESH_PLANNED_OBSERVED_TYPES, sf, by_cs,
                copy_type_tuple(tc, by_cs->cs, by_type->arg_types), evidence, 1);
            unaccounted_hits -= by_type->hits;
            unaccounted_osr_hits -= by_type->osr_hits;
        }
        else {
            /* TODO derived specialization planning */
        }
    }

    /* If there are enough unaccounted for hits by type specializations, then
     * plan a certain specialization. */
    if (unaccounted_hits && unaccounted_hits >= MVM_spesh_threshold(tc, sf) ||
            unaccounted_osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
        add_planned(tc, plan, MVM_SPESH_PLANNED_CERTAIN, sf, by_cs, NULL, NULL, 0);
}

/* Considers the statistics of a given static frame and plans specializtions
 * to produce for it. */
void plan_for_sf(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf) {
    MVMSpeshStats *ss = sf->body.spesh->body.spesh_stats;
    MVMuint32 threshold = MVM_spesh_threshold(tc, sf);
    if (ss->hits >= threshold || ss->osr_hits >= MVM_SPESH_PLAN_SF_MIN_OSR) {
        /* The frame is hot enough; look through its callsites to see if any
         * of those are. */
        MVMuint32 i;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            if (by_cs->hits >= threshold || by_cs->osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
                plan_for_cs(tc, plan, sf, by_cs);
        }
    }
}

/* Sorts the plan in descending order of maximum call depth. */
void sort_plan(MVMThreadContext *tc, MVMSpeshPlanned *planned, MVMuint32 n) {
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
MVMSpeshPlan * MVM_spesh_plan(MVMThreadContext *tc, MVMObject *updated_static_frames) {
    MVMSpeshPlan *plan = MVM_calloc(1, sizeof(MVMSpeshPlan));
    MVMint64 updated = MVM_repr_elems(tc, updated_static_frames);
    MVMint64 i;
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
    for (i = 0; i < updated; i++) {
        MVMObject *sf = MVM_repr_at_pos_o(tc, updated_static_frames, i);
        plan_for_sf(tc, plan, (MVMStaticFrame *)sf);
    }
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

/* Frees all memory associated with a specialization plan. */
void MVM_spesh_plan_destroy(MVMThreadContext *tc, MVMSpeshPlan *plan) {
    MVMuint32 i;
    for (i = 0; i < plan->num_planned; i++)
        MVM_free(plan->planned[i].type_tuple);
    MVM_free(plan->planned);
    MVM_free(plan);
}
