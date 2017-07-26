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

    /* Argument types logged. Sized by number of callsite flags. */
    MVMSpeshStatsType *arg_types;

    /* Spesh log entries for types and values, for later processing. */
    MVMSpeshLogEntry **offset_logs;
    MVMuint32 offset_logs_used;
    MVMuint32 offset_logs_limit;

    /* Number of types we crossed an OSR point. */
    MVMuint32 osr_hits;
} SimStackFrame;
typedef struct SimStack {
    /* Array of frames. */
    SimStackFrame *frames;

    /* Current frame index and allocated space. */
    MVMuint32 used;
    MVMuint32 limit;

    /* Current stack depth. */
    MVMuint32 depth;
} SimStack;

/* Gets the statistics for a static frame, creating them if needed. */
MVMSpeshStats * stats_for(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.spesh_stats)
        sf->body.spesh_stats = MVM_calloc(1, sizeof(MVMSpeshStats));
    return sf->body.spesh_stats;
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

/* Checks if a type tuple is incomplete (no types logged for some passed
 * objects, or no decont type logged for a container type). */
MVMint32 incomplete_type_tuple(MVMThreadContext *tc, MVMCallsite *cs,
                               MVMSpeshStatsType *arg_types) {
    MVMuint32 i;
    for (i = 0; i < cs->flag_count; i++) {
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ) {
            MVMObject *type = arg_types[i].type;
            if (!type)
                return 1;
            if (arg_types[i].type_concrete && type->st->container_spec)
                if (!arg_types[i].decont_type)
                    return 1;
        }
    }
    return 0;
}

/* Returns true if the callsite has no object arguments, false otherwise. */
MVMint32 cs_without_object_args(MVMThreadContext *tc, MVMCallsite *cs) {
    MVMuint32 i;
    for (i = 0; i < cs->flag_count; i++)
        if (cs->arg_flags[i] & MVM_CALLSITE_ARG_OBJ)
            return 0;
    return 1;
}

/* Gets the stats by type, adding it if it's missing. Frees arg_types. */
MVMSpeshStatsByType * by_type(MVMThreadContext *tc, MVMSpeshStats *ss, MVMuint32 callsite_idx,
                              MVMSpeshStatsType *arg_types) {
    /* Resolve type by callsite level info. If this is the no-callsite
     * specialization or this callsite has no object arguments, there is
     * nothing further to do. */
    MVMSpeshStatsByCallsite *css = &(ss->by_callsite[callsite_idx]);
    MVMCallsite *cs = css->cs;
    if (!cs || cs_without_object_args(tc, cs)) {
        MVM_free(arg_types);
        return NULL;
    }
    else if (incomplete_type_tuple(tc, cs, arg_types)) {
        /* Type tuple is incomplete, maybe because the log buffer ended prior
         * to having all the type information. Discard. */
        MVM_free(arg_types);
        return NULL;
    }
    else {
        /* See if we already have it. */
        size_t args_length = cs->flag_count * sizeof(MVMSpeshStatsType);
        MVMuint32 found;
        MVMuint32 n = css->num_by_type;
        for (found = 0; found < n; found++) {
            if (memcmp(css->by_type[found].arg_types, arg_types, args_length) == 0) {
                MVM_free(arg_types);
                return &(css->by_type[found]);
            }
        }

        /* If not, we need a new record. */
        found = css->num_by_type;
        css->num_by_type++;
        css->by_type = MVM_realloc(css->by_type,
            css->num_by_type * sizeof(MVMSpeshStatsByType));
        memset(&(css->by_type[found]), 0, sizeof(MVMSpeshStatsByType));
        css->by_type[found].arg_types = arg_types;
        return &(css->by_type[found]);
    }
}

/* Get the stats by offset entry, adding it if it's missing. */
MVMSpeshStatsByOffset * by_offset(MVMThreadContext *tc, MVMSpeshStatsByType *tss,
                                  MVMuint32 bytecode_offset) {
    /* See if we already have it. */
    MVMuint32 found;
    MVMuint32 n = tss->num_by_offset;
    for (found = 0; found < n; found++)
        if (tss->by_offset[found].bytecode_offset == bytecode_offset)
            return &(tss->by_offset[found]);

    /* If not, we need a new record. */
    found = tss->num_by_offset;
    tss->num_by_offset++;
    tss->by_offset = MVM_realloc(tss->by_offset,
        tss->num_by_offset * sizeof(MVMSpeshStatsByOffset));
    memset(&(tss->by_offset[found]), 0, sizeof(MVMSpeshStatsByOffset));
    tss->by_offset[found].bytecode_offset = bytecode_offset;
    return &(tss->by_offset[found]);
}

/* Adds/increments the count of a certain type seen at the given offset. */
void add_type_at_offset(MVMThreadContext *tc, MVMSpeshStatsByOffset *oss,
                        MVMStaticFrame *sf, MVMObject *type, MVMuint8 concrete) {
    /* If we have it already, increment the count. */
    MVMuint32 found;
    MVMuint32 n = oss->num_types;
    for (found = 0; found < n; found++) {
        if (oss->types[found].type == type && oss->types[found].type_concrete == concrete) {
            oss->types[found].count++;
            return;
        }
    }

    /* Otherwise, add it to the list. */
    found = oss->num_types;
    oss->num_types++;
    oss->types = MVM_realloc(oss->types, oss->num_types * sizeof(MVMSpeshStatsTypeCount));
    MVM_ASSIGN_REF(tc, &(sf->common.header), oss->types[found].type, type);
    oss->types[found].type_concrete = concrete;
    oss->types[found].count = 1;
}

/* Adds/increments the count of a certain value seen at the given offset. */
void add_value_at_offset(MVMThreadContext *tc, MVMSpeshStatsByOffset *oss,
                         MVMStaticFrame *sf, MVMObject *value) {
    /* If we have it already, increment the count. */
    MVMuint32 found;
    MVMuint32 n = oss->num_values;
    for (found = 0; found < n; found++) {
        if (oss->values[found].value == value) {
            oss->values[found].count++;
            return;
        }
    }

    /* Otherwise, add it to the list. */
    found = oss->num_values;
    oss->num_values++;
    oss->values = MVM_realloc(oss->values, oss->num_values * sizeof(MVMSpeshStatsValueCount));
    MVM_ASSIGN_REF(tc, &(sf->common.header), oss->values[found].value, value);
    oss->values[found].count = 1;
}

/* Initializes the stack simulation. */
void sim_stack_init(MVMThreadContext *tc, SimStack *sims) {
    sims->used = 0;
    sims->limit = 32;
    sims->frames = MVM_malloc(sims->limit * sizeof(SimStackFrame));
    sims->depth = 0;
}

/* Pushes an entry onto the stack frame model. */
void sim_stack_push(MVMThreadContext *tc, SimStack *sims, MVMStaticFrame *sf,
                    MVMSpeshStats *ss, MVMuint32 cid, MVMuint32 callsite_idx) {
    SimStackFrame *frame;
    MVMCallsite *cs;
    if (sims->used == sims->limit) {
        sims->limit *= 2;
        sims->frames = MVM_realloc(sims->frames, sims->limit * sizeof(SimStackFrame));
    }
    frame = &(sims->frames[sims->used++]);
    frame->sf = sf;
    frame->ss = ss;
    frame->cid = cid;
    frame->callsite_idx = callsite_idx;
    frame->arg_types = (cs = ss->by_callsite[callsite_idx].cs)
        ? MVM_calloc(cs->flag_count, sizeof(MVMSpeshStatsType))
        : NULL;
    frame->offset_logs = NULL;
    frame->offset_logs_used = frame->offset_logs_limit = 0;
    frame->osr_hits = 0;
    sims->depth++;
}

/* Pops the top frame from the sim stack. */
void sim_stack_pop(MVMThreadContext *tc, SimStack *sims) {
    SimStackFrame *simf;
    MVMSpeshStatsByType *tss;
    MVMuint32 frame_depth;

    /* Pop off the simulated frame. */
    if (sims->used == 0)
        MVM_panic(1, "Spesh stats: cannot pop an empty simulation stack");
    sims->used--;
    simf = &(sims->frames[sims->used]);
    frame_depth = sims->depth--;

    /* Add OSR hits at callsite level and update depth. */
    if (simf->osr_hits) {
        simf->ss->osr_hits += simf->osr_hits;
        simf->ss->by_callsite[simf->callsite_idx].osr_hits += simf->osr_hits;
    }
    if (frame_depth > simf->ss->by_callsite[simf->callsite_idx].max_depth)
        simf->ss->by_callsite[simf->callsite_idx].max_depth = frame_depth;

    /* Update the by-type record, incorporating by-offset data. */
    tss = by_type(tc, simf->ss, simf->callsite_idx, simf->arg_types);
    if (tss) {
        MVMuint32 i;
        for (i = 0; i < simf->offset_logs_used; i++) {
            MVMSpeshLogEntry *e = simf->offset_logs[i];
            switch (e->kind) {
                case MVM_SPESH_LOG_TYPE: {
                    MVMSpeshStatsByOffset *oss = by_offset(tc, tss,
                        e->type.bytecode_offset);
                    add_type_at_offset(tc, oss, simf->sf, e->type.type,
                        e->type.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE);
                    break;
                }
                case MVM_SPESH_LOG_INVOKE: {
                    MVMSpeshStatsByOffset *oss = by_offset(tc, tss,
                        e->value.bytecode_offset);
                    add_value_at_offset(tc, oss, simf->sf, e->value.value);
                    break;
                }
            }
        }
        tss->hits++;
        tss->osr_hits += simf->osr_hits;
        if (frame_depth > tss->max_depth)
            tss->max_depth = frame_depth;
    }

    /* Clear up offset logs; they're either incorproated or to be tossed. */
    MVM_free(simf->offset_logs);
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
    while (sims->used)
        sim_stack_pop(tc, sims);
    MVM_free(sims->frames);
}

/* Gets the parameter type slot from a simulation frame. */
MVMSpeshStatsType * param_type(MVMThreadContext *tc, SimStackFrame *simf, MVMSpeshLogEntry *e) {
    MVMuint16 idx = e->param.arg_idx;
    MVMCallsite *cs = simf->ss->by_callsite[simf->callsite_idx].cs;
    if (cs) {
        MVMint32 flag_idx = idx < cs->num_pos
            ? idx
            : cs->num_pos + (((idx - 1) - cs->num_pos) / 2);
        if (flag_idx >= cs->flag_count)
            MVM_panic(1, "Spesh stats: argument flag index out of bounds");
        if (cs->arg_flags[flag_idx] & MVM_CALLSITE_ARG_OBJ)
            return &(simf->arg_types[flag_idx]);
    }
    return NULL;
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
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
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
                if (simf) {
                    MVMSpeshStatsType *type_slot = param_type(tc, simf, e);
                    if (type_slot) {
                        MVM_ASSIGN_REF(tc, &(simf->sf->common.header), type_slot->type,
                            e->param.type);
                        type_slot->type_concrete =
                            e->param.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE;
                    }
                }
                break;
            }
            case MVM_SPESH_LOG_PARAMETER_DECONT: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                if (simf) {
                    MVMSpeshStatsType *type_slot = param_type(tc, simf, e);
                    if (type_slot) {
                        MVM_ASSIGN_REF(tc, &(simf->sf->common.header), type_slot->decont_type,
                            e->param.type);
                        type_slot->decont_type_concrete =
                            e->param.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE;
                    }
                }
                break;
            }
            case MVM_SPESH_LOG_TYPE:
            case MVM_SPESH_LOG_INVOKE: {
                /* We only incorporate these into the model later, and only
                 * then if we need to. For now, just mkeep references to
                 * them. */
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                if (simf) {
                    if (simf->offset_logs_used == simf->offset_logs_limit) {
                        simf->offset_logs_limit += 32;
                        simf->offset_logs = MVM_realloc(simf->offset_logs,
                            simf->offset_logs_limit * sizeof(MVMSpeshLogEntry *));
                    }
                    simf->offset_logs[simf->offset_logs_used++] = e;
                }
                break;
            }
            case MVM_SPESH_LOG_OSR: {
                SimStackFrame *simf = sim_stack_find(tc, &sims, e->id);
                if (simf)
                    simf->osr_hits++;
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
#if MVM_GC_DEBUG
    tc->in_spesh = 0;
#endif
}

/* Takes an array of frames we recently updated the stats in. If they weren't
 * updated in a while, clears them out. */
void MVM_spesh_stats_cleanup(MVMThreadContext *tc, MVMObject *check_frames) {
    MVMint64 elems = MVM_repr_elems(tc, check_frames);
    MVMint64 insert_pos = 0;
    MVMint64 i;
    for (i = 0; i < elems; i++) {
        MVMStaticFrame *sf = (MVMStaticFrame *)MVM_repr_at_pos_o(tc, check_frames, i);
        MVMSpeshStats *ss = sf->body.spesh_stats;
        if (!ss) {
            /* No stats; already destroyed, don't keep this frame under
             * consideration. */
        }
        else if (tc->instance->spesh_stats_version - ss->last_update > MVM_SPESH_STATS_MAX_AGE) {
            MVM_spesh_stats_destroy(tc, ss);
            sf->body.spesh_stats = NULL;
        }
        else {
            MVM_repr_bind_pos_o(tc, check_frames, insert_pos++, (MVMObject *)sf);
        }
    }
    MVM_repr_pos_set_elems(tc, check_frames, insert_pos);
}

void MVM_spesh_stats_gc_mark(MVMThreadContext *tc, MVMSpeshStats *ss, MVMGCWorklist *worklist) {
    if (ss) {
        MVMuint32 i, j, k, l;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            for (j = 0; j < by_cs->num_by_type; j++) {
                MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                MVMuint32 num_types = by_cs->cs->flag_count;
                for (k = 0; k < num_types; k++) {
                    MVM_gc_worklist_add(tc, worklist, &(by_type->arg_types[k].type));
                    MVM_gc_worklist_add(tc, worklist, &(by_type->arg_types[k].decont_type));
                }
                for (k = 0; k < by_type->num_by_offset; k++) {
                    MVMSpeshStatsByOffset *by_offset = &(by_type->by_offset[k]);
                    for (l = 0; l < by_offset->num_types; l++)
                        MVM_gc_worklist_add(tc, worklist, &(by_offset->types[l].type));
                    for (l = 0; l < by_offset->num_values; l++)
                        MVM_gc_worklist_add(tc, worklist, &(by_offset->values[l].value));
                }
            }
        }
        for (i = 0; i < ss->num_static_values; i++)
            MVM_gc_worklist_add(tc, worklist, &(ss->static_values[i].value));
    }
}

void MVM_spesh_stats_destroy(MVMThreadContext *tc, MVMSpeshStats *ss) {
    if (ss) {
        MVMuint32 i, j, k;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            for (j = 0; j < by_cs->num_by_type; j++) {
                MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                for (k = 0; k < by_type->num_by_offset; k++) {
                    MVMSpeshStatsByOffset *by_offset = &(by_type->by_offset[k]);
                    MVM_free(by_offset->types);
                    MVM_free(by_offset->values);
                }
                MVM_free(by_type->by_offset);
                MVM_free(by_type->arg_types);
            }
            MVM_free(by_cs->by_type);
        }
        MVM_free(ss->by_callsite);
        MVM_free(ss->static_values);
    }
}
