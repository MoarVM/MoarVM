#include "moar.h"

/* Gets the statistics for a static frame, creating them if needed. */
MVMSpeshStats * stats_for(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.spesh_stats)
        sf->body.spesh_stats = MVM_calloc(1, sizeof(MVMSpeshStats));
    return sf->body.spesh_stats;
}

/* Receives a spesh log and updates static frame statistics. Each static frame
 * that is updated is pushed once into sf_updated. */
void MVM_spesh_stats_update(MVMThreadContext *tc, MVMSpeshLog *sl, MVMObject *sf_updated) {
    MVMuint32 i;
    MVMuint32 n = sl->body.used;
    for (i = 0; i < n; i++) {
        MVMSpeshLogEntry *e = &(sl->body.entries[i]);
        switch (e->kind) {
            case MVM_SPESH_LOG_ENTRY: {
                MVMSpeshStats *ss = stats_for(tc, e->entry.sf);
                if (ss->last_update != tc->instance->spesh_stats_version) {
                    ss->last_update = tc->instance->spesh_stats_version;
                    MVM_repr_push_o(tc, sf_updated, (MVMObject *)e->entry.sf);
                }
                ss->hits++;
                break;
            }
        }
    }
}

void MVM_spesh_stats_gc_mark(MVMThreadContext *tc, MVMSpeshStats *ss, MVMGCWorklist *worklist) {
    if (ss) {
        MVMuint32 i, j, k;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            for (j = 0; j < by_cs->num_by_type; j++) {
                MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                MVMuint32 num_types = by_cs->cs->flag_count;
                for (k = 0; k < num_types; k++) {
                    MVM_gc_worklist_add(tc, worklist, &(by_type->arg_types[k].type));
                    MVM_gc_worklist_add(tc, worklist, &(by_type->arg_types[k].decont_type));
                }
            }
        }
        for (i = 0; i < ss->num_static_values; i++)
            MVM_gc_worklist_add(tc, worklist, &(ss->static_values[i].value));
    }
}

void MVM_spesh_stats_destroy(MVMThreadContext *tc, MVMSpeshStats *ss) {
    if (ss) {
        MVMuint32 i, j;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            for (j = 0; j < by_cs->num_by_type; j++) {
                MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                MVM_free(by_type->arg_types);
            }
            MVM_free(by_cs->by_type);
        }
        MVM_free(ss->by_callsite);
        MVM_free(ss->static_values);
    }
}
