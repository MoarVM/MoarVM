#include "moar.h"

/* Adds a planned specialization. */
void add_planned(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMSpeshPlannedKind kind,
                 MVMStaticFrame *sf, MVMSpeshStatsByCallsite *cs_stats,
                 MVMSpeshStatsType *type_tuple, MVMSpeshStatsByType **type_stats,
                 MVMuint32 num_type_stats) {
    MVMSpeshPlanned *p;
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
}

/* Considers the statistics of a given callsite + static frame pairing and
 * plans specializations to produce for it. */
void plan_for_cs(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf,
                 MVMSpeshStatsByCallsite *by_cs) {
    if (by_cs->hits >= MVM_SPESH_PLAN_CS_MIN || by_cs->osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR) {
        /* This callsite is hot enough. See if any types tuples are hot
         * enough. */
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
                    by_type->arg_types, evidence, 1);
                unaccounted_hits -= by_type->hits;
                unaccounted_osr_hits -= by_type->osr_hits;
            }
            else {
                /* TODO derived specialization planning */
            }
        }

        /* If there are enough unaccounted for hits by type specializations, then
         * plan a certain specialization. */
        if (unaccounted_hits >= MVM_SPESH_PLAN_CS_MIN ||
                unaccounted_osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
            add_planned(tc, plan, MVM_SPESH_PLANNED_CERTAIN, sf, by_cs, NULL, NULL, 0);
    }
}

/* Considers the statistics of a given static frame and plans specializtions
 * to produce for it. */
void plan_for_sf(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf) {
    MVMSpeshStats *ss = sf->body.spesh_stats;
    if (ss->hits >= MVM_SPESH_PLAN_SF_MIN || ss->osr_hits >= MVM_SPESH_PLAN_SF_MIN_OSR) {
        /* The frame is hot enough; look through its callsites. */
        MVMuint32 i;
        for (i = 0; i < ss->num_by_callsite; i++)
            plan_for_cs(tc, plan, sf, &(ss->by_callsite[i]));
    }
}

/* Forms a specialization plan from considering all frames whose statics have
 * changed. */
MVMSpeshPlan * MVM_spesh_plan(MVMThreadContext *tc, MVMObject *updated_static_frames) {
    MVMSpeshPlan *plan = MVM_calloc(1, sizeof(MVMSpeshPlan));
    MVMint64 updated = MVM_repr_elems(tc, updated_static_frames);
    MVMint64 i;
    for (i = 0; i < updated; i++) {
        MVMObject *sf = MVM_repr_at_pos_o(tc, updated_static_frames, i);
        plan_for_sf(tc, plan, (MVMStaticFrame *)sf);
    }
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
