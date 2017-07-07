#include "moar.h"

/* Considers the statistics of a given static frame and plans specializtions
 * to produce for it. */
void plan_for(MVMThreadContext *tc, MVMSpeshPlan *plan, MVMStaticFrame *sf) {
}

/* Forms a specialization plan from considering all frames whose statics have
 * changed. */
MVMSpeshPlan * MVM_spesh_plan(MVMThreadContext *tc, MVMObject *updated_static_frames) {
    MVMSpeshPlan *plan = MVM_calloc(1, sizeof(MVMSpeshPlan));
    MVMint64 updated = MVM_repr_elems(tc, updated_static_frames);
    MVMint64 i;
    for (i = 0; i < updated; i++) {
        MVMObject *sf = MVM_repr_at_pos_o(tc, updated_static_frames, i);
        plan_for(tc, plan, (MVMStaticFrame *)sf);
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
