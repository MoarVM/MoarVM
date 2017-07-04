#include "moar.h"

/* Logs are linear recordings marked with frame correlation IDs. We need to
 * simulate the call stack as part of the analysis. This models a frame on
 * the call stack and the stack respectively. */
typedef struct SimStackFrame {
    /* The static frame. */
    MVMStaticFrame *sf;

    /* Spesh stats for the stack frame. */
    MVMSpeshStats *ss;

    /* Correlation ID. */
    MVMuint32 cid;

    /* Callsite stats index (not pointer in case of realloc). */
    MVMuint32 callsite_idx;
} SimStackFrame;
typedef struct SimStack {
    /* Array of frames. */
    SimStackFrame *frames;

    /* Current frame index and allocated space. */
    MVMuint32 used;
    MVMuint32 limit;
} SimStack;

/* Gets the statistics for a static frame, creating them if needed. */
MVMSpeshStats * stats_for(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.spesh_stats)
        sf->body.spesh_stats = MVM_calloc(1, sizeof(MVMSpeshStats));
    return sf->body.spesh_stats;
}

/* Initializes the stack simulation. */
void sim_stack_init(MVMThreadContext *tc, SimStack *sims) {
    sims->used = 0;
    sims->limit = 32;
    sims->frames = MVM_malloc(sims->limit * sizeof(SimStackFrame));
}

/* Pushes an entry onto the stack frame model. */
void sim_stack_push(MVMThreadContext *tc, SimStack *sims, MVMStaticFrame *sf,
                    MVMSpeshStats *ss, MVMuint32 cid, MVMuint32 callsite_idx) {
    SimStackFrame *frame;
    if (sims->used == sims->limit) {
        sims->limit *= 2;
        sims->frames = MVM_realloc(sims->frames, sims->limit * sizeof(SimStackFrame));
    }
    frame = &(sims->frames[sims->used++]);
    frame->sf = sf;
    frame->ss = ss;
    frame->cid = cid;
    frame->callsite_idx = callsite_idx;
}

/* Pops the top frame from the sim stack. */
void sim_stack_pop(MVMThreadContext *tc, SimStack *sims) {
    if (sims->used == 0)
        MVM_panic(1, "Spesh stats: cannot pop an empty simulation stack");
    sims->used--;
}

/* Gets the simulation stack frame for the specified correlation ID. If it is
 * not on the top, searches to see if it's further down. If it is, then pops
 * off the top to reach it. If it's not found at all, returns NULL and does
 * nothing to the simulation stack. */
SimStackFrame * sim_stack_find(MVMThreadContext *tc, SimStack *sims, MVMuint32 cid) {
    MVMuint32 found_at = sims->used;
    while (found_at != 0) {
        found_at--;
        if (sims->frames[found_at].cid == cid) {
            MVMint32 pop = (sims->used - found_at) - 1;
            MVMint32 i;
            for (i = 0; i < pop; i++)
                sim_stack_pop(tc, sims);
            return &(sims->frames[found_at]);
        }
    }
    return NULL;
}

/* Destroys the stack simulation. */
void sim_stack_destroy(MVMThreadContext *tc, SimStack *sims) {
    MVM_free(sims->frames);
}

/* Gets the stats by callsite, adding it if it's missing. */
MVMuint32 by_callsite_idx(MVMThreadContext *tc, MVMSpeshStats *ss, MVMCallsite *cs) {
    /* See if we already have it. */
    MVMuint32 found;
    MVMuint32 n = ss->num_by_callsite;
    for (found = 0; found < n; found++)
        if (ss->by_callsite[found].cs == cs)
            return found;

    /* If not, we need a new record. */
    found = ss->num_by_callsite;
    ss->num_by_callsite++;
    ss->by_callsite = MVM_realloc(ss->by_callsite,
        ss->num_by_callsite * sizeof(MVMSpeshStatsByCallsite));
    memset(&(ss->by_callsite[found]), 0, sizeof(MVMSpeshStatsByCallsite));
    ss->by_callsite[found].cs = cs;
    return found;
}

/* Records a static value for a frame, unless it's already in the log. */
void add_static_value(MVMThreadContext *tc, SimStackFrame *simf, MVMint32 bytecode_offset,
                      MVMObject *value) {
    MVMSpeshStats *ss = simf->ss;
    MVMuint32 i, id;
    for (i = 0; i < ss->num_static_values; i++)
        if (ss->static_values[i].bytecode_offset == bytecode_offset)
            return;
    id = ss->num_static_values++;
    ss->static_values = MVM_realloc(ss->static_values,
        ss->num_static_values * sizeof(MVMSpeshStatsStatic));
    ss->static_values[id].bytecode_offset = bytecode_offset;
    MVM_ASSIGN_REF(tc, &(simf->sf->common.header), ss->static_values[id].value, value);
}

/* Receives a spesh log and updates static frame statistics. Each static frame
 * that is updated is pushed once into sf_updated. */
void MVM_spesh_stats_update(MVMThreadContext *tc, MVMSpeshLog *sl, MVMObject *sf_updated) {
    MVMuint32 i;
    MVMuint32 n = sl->body.used;
    SimStack sims;
    sim_stack_init(tc, &sims);
    for (i = 0; i < n; i++) {
        MVMSpeshLogEntry *e = &(sl->body.entries[i]);
        switch (e->kind) {
            case MVM_SPESH_LOG_ENTRY: {
                MVMSpeshStats *ss = stats_for(tc, e->entry.sf);
                MVMuint32 callsite_idx;
                if (ss->last_update != tc->instance->spesh_stats_version) {
                    ss->last_update = tc->instance->spesh_stats_version;
                    MVM_repr_push_o(tc, sf_updated, (MVMObject *)e->entry.sf);
                }
                ss->hits++;
                callsite_idx = by_callsite_idx(tc, ss, e->entry.cs);
                ss->by_callsite[callsite_idx].hits++;
                sim_stack_push(tc, &sims, e->entry.sf, ss, e->id, callsite_idx);
                break;
            }
            case MVM_SPESH_LOG_PARAMETER: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                /* TODO Add to parameters logging */
                break;
            }
            case MVM_SPESH_LOG_PARAMETER_DECONT: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                /* TODO Add to parameters logging */
                break;
            }
            case MVM_SPESH_LOG_TYPE:
            case MVM_SPESH_LOG_INVOKE:
            case MVM_SPESH_LOG_OSR: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                /* TODO Stash entry for later association */
                break;
            }
            case MVM_SPESH_LOG_STATIC: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                if (simf)
                    add_static_value(tc, simf, e->value.bytecode_offset, e->value.value);
                break;
            }
        }
    }
    sim_stack_destroy(tc, &sims);
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
