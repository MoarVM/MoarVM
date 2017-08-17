#include "moar.h"

/* Gets the statistics for a static frame, creating them if needed. */
MVMSpeshStats * stats_for(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMStaticFrameSpesh *spesh = sf->body.spesh;
    if (!spesh->body.spesh_stats)
        spesh->body.spesh_stats = MVM_calloc(1, sizeof(MVMSpeshStats));
    return spesh->body.spesh_stats;
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

/* Gets the stats by type, adding it if it's missing. Frees arg_types. Returns
 * the index in the by type array, or -1 if unresolved. */
MVMint32 by_type(MVMThreadContext *tc, MVMSpeshStats *ss, MVMuint32 callsite_idx,
                  MVMSpeshStatsType *arg_types) {
    /* Resolve type by callsite level info. If this is the no-callsite
     * specialization there is nothing further to do. */
    MVMSpeshStatsByCallsite *css = &(ss->by_callsite[callsite_idx]);
    MVMCallsite *cs = css->cs;
    if (!cs) {
        MVM_free(arg_types);
        return -1;
    }

    /* Otherwise if there's no object args, then we'll use a single "by type"
     * specialization, so we can have data tracked by offset at least. */
    else if (cs_without_object_args(tc, cs)) {
        if (css->num_by_type == 0) {
            css->num_by_type++;
            css->by_type = MVM_calloc(1, sizeof(MVMSpeshStatsByType));
            css->by_type[0].arg_types = arg_types;
        }
        else {
            MVM_free(arg_types);
        }
        return 0;
    }

    /* Maybe the type tuple is incomplete, maybe because the log buffer ended
     * prior to having all the type information. Discard. */
    else if (incomplete_type_tuple(tc, cs, arg_types)) {
        MVM_free(arg_types);
        return -1;
    }

    /* Can produce by-type stats. */
    else {
        /* See if we already have it. */
        size_t args_length = cs->flag_count * sizeof(MVMSpeshStatsType);
        MVMuint32 found;
        MVMuint32 n = css->num_by_type;
        for (found = 0; found < n; found++) {
            if (memcmp(css->by_type[found].arg_types, arg_types, args_length) == 0) {
                MVM_free(arg_types);
                return found;
            }
        }

        /* If not, we need a new record. */
        found = css->num_by_type;
        css->num_by_type++;
        css->by_type = MVM_realloc(css->by_type,
            css->num_by_type * sizeof(MVMSpeshStatsByType));
        memset(&(css->by_type[found]), 0, sizeof(MVMSpeshStatsByType));
        css->by_type[found].arg_types = arg_types;
        return found;
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
    MVM_ASSIGN_REF(tc, &(sf->body.spesh->common.header), oss->types[found].type, type);
    oss->types[found].type_concrete = concrete;
    oss->types[found].count = 1;
}

/* Adds/increments the count of a certain invocation target seen at the given
 * offset. */
void add_invoke_at_offset(MVMThreadContext *tc, MVMSpeshStatsByOffset *oss,
                          MVMStaticFrame *sf, MVMStaticFrame *target_sf,
                          MVMint16 caller_is_outer, MVMint16 was_multi) {
    /* If we have it already, increment the count. */
    MVMuint32 found;
    MVMuint32 n = oss->num_invokes;
    for (found = 0; found < n; found++) {
        if (oss->invokes[found].sf == target_sf) {
            oss->invokes[found].count++;
            if (caller_is_outer)
                oss->invokes[found].caller_is_outer_count++;
            if (was_multi)
                oss->invokes[found].was_multi_count++;
            return;
        }
    }

    /* Otherwise, add it to the list. */
    found = oss->num_invokes;
    oss->num_invokes++;
    oss->invokes = MVM_realloc(oss->invokes,
        oss->num_invokes * sizeof(MVMSpeshStatsInvokeCount));
    MVM_ASSIGN_REF(tc, &(sf->body.spesh->common.header), oss->invokes[found].sf, target_sf);
    oss->invokes[found].count = 1;
    oss->invokes[found].caller_is_outer_count = caller_is_outer ? 1 : 0;
    oss->invokes[found].was_multi_count = was_multi ? 1 : 0;
}

/* Adds/increments the count of a type tuple seen at the given offset. */
void add_type_tuple_at_offset(MVMThreadContext *tc, MVMSpeshStatsByOffset *oss,
                              MVMStaticFrame *sf, MVMSpeshSimCallType *info) {
    /* Compute type tuple size. */
    size_t tt_size = info->cs->flag_count * sizeof(MVMSpeshStatsType);

    /* If we have it already, increment the count. */
    MVMuint32 found, i;
    MVMuint32 n = oss->num_type_tuples;
    for (found = 0; found < n; found++) {
        if (oss->type_tuples[found].cs == info->cs) {
            if (memcmp(oss->type_tuples[found].arg_types, info->arg_types, tt_size) == 0) {
                oss->type_tuples[found].count++;
                return;
            }
        }
    }

    /* Otherwise, add it to the list; copy type tuple to ease memory
     * management, but also need to write barrier any types. */
    found = oss->num_type_tuples;
    oss->num_type_tuples++;
    oss->type_tuples = MVM_realloc(oss->type_tuples,
        oss->num_type_tuples * sizeof(MVMSpeshStatsTypeTupleCount));
    oss->type_tuples[found].cs = info->cs;
    oss->type_tuples[found].arg_types = MVM_malloc(tt_size);
    memcpy(oss->type_tuples[found].arg_types, info->arg_types, tt_size);
    for (i = 0; i < info->cs->flag_count; i++) {
        if (info->arg_types[i].type)
            MVM_gc_write_barrier(tc, &(sf->body.spesh->common.header),
                &(info->arg_types[i].type->header));
        if (info->arg_types[i].decont_type)
            MVM_gc_write_barrier(tc, &(sf->body.spesh->common.header),
                &(info->arg_types[i].decont_type->header));
    }
    oss->type_tuples[found].count = 1;
}

/* Initializes the stack simulation. */
void sim_stack_init(MVMThreadContext *tc, MVMSpeshSimStack *sims) {
    sims->used = 0;
    sims->limit = 32;
    sims->frames = MVM_malloc(sims->limit * sizeof(MVMSpeshSimStackFrame));
    sims->depth = 0;
}

/* Pushes an entry onto the stack frame model. */
void sim_stack_push(MVMThreadContext *tc, MVMSpeshSimStack *sims, MVMStaticFrame *sf,
                    MVMSpeshStats *ss, MVMuint32 cid, MVMuint32 callsite_idx) {
    MVMSpeshSimStackFrame *frame;
    MVMCallsite *cs;
    if (sims->used == sims->limit) {
        sims->limit *= 2;
        sims->frames = MVM_realloc(sims->frames, sims->limit * sizeof(MVMSpeshSimStackFrame));
    }
    frame = &(sims->frames[sims->used++]);
    frame->sf = sf;
    frame->ss = ss;
    frame->cid = cid;
    frame->callsite_idx = callsite_idx;
    frame->type_idx = -1;
    frame->arg_types = (cs = ss->by_callsite[callsite_idx].cs)
        ? MVM_calloc(cs->flag_count, sizeof(MVMSpeshStatsType))
        : NULL;
    frame->offset_logs = NULL;
    frame->offset_logs_used = frame->offset_logs_limit = 0;
    frame->osr_hits = 0;
    frame->call_type_info = NULL;
    frame->call_type_info_used = frame->call_type_info_limit = 0;
    frame->last_invoke_offset = 0;
    frame->last_invoke_sf = NULL;
    sims->depth++;
}

/* Adds an entry to a sim frame's callsite type info list, for later
 * inclusion in the callsite stats. */
void add_sim_call_type_info(MVMThreadContext *tc, MVMSpeshSimStackFrame *simf,
                            MVMuint32 bytecode_offset, MVMCallsite *cs,
                            MVMSpeshStatsType *arg_types) {
    MVMSpeshSimCallType *info;
    if (simf->call_type_info_used == simf->call_type_info_limit) {
        simf->call_type_info_limit += 32;
        simf->call_type_info = MVM_realloc(simf->call_type_info,
            simf->call_type_info_limit * sizeof(MVMSpeshSimCallType));
    }
    info = &(simf->call_type_info[simf->call_type_info_used++]);
    info->bytecode_offset = bytecode_offset;
    info->cs = cs;
    info->arg_types = arg_types;
}

/* Incorporate information collected into a sim stack frame. */
void incorporate_stats(MVMThreadContext *tc, MVMSpeshSimStackFrame *simf,
                       MVMuint32 frame_depth, MVMSpeshSimStackFrame *caller,
                       MVMObject *sf_updated) {
    MVMSpeshStatsByType *tss;
    MVMint32 first_type_hit = 0;

    /* Bump version if needed. */
    if (simf->ss->last_update != tc->instance->spesh_stats_version) {
        simf->ss->last_update = tc->instance->spesh_stats_version;
        MVM_repr_push_o(tc, sf_updated, (MVMObject *)simf->sf);
    }

    /* Add OSR hits at callsite level and update depth. */
    if (simf->osr_hits) {
        simf->ss->osr_hits += simf->osr_hits;
        simf->ss->by_callsite[simf->callsite_idx].osr_hits += simf->osr_hits;
    }
    if (frame_depth > simf->ss->by_callsite[simf->callsite_idx].max_depth)
        simf->ss->by_callsite[simf->callsite_idx].max_depth = frame_depth;

    /* See if there's a type tuple to attach type-based stats to. */
    if (simf->type_idx < 0 && simf->arg_types) {
        simf->type_idx = by_type(tc, simf->ss, simf->callsite_idx, simf->arg_types);
        simf->arg_types = NULL;
        first_type_hit = 1;
    }
    tss = simf->type_idx >= 0
        ? &(simf->ss->by_callsite[simf->callsite_idx].by_type[simf->type_idx])
        : NULL;
    if (tss) {
        /* Incorporate data logged at offsets. */
        MVMuint32 i;
        for (i = 0; i < simf->offset_logs_used; i++) {
            MVMSpeshLogEntry *e = simf->offset_logs[i];
            switch (e->kind) {
                case MVM_SPESH_LOG_TYPE:
                case MVM_SPESH_LOG_RETURN: {
                    MVMSpeshStatsByOffset *oss = by_offset(tc, tss,
                        e->type.bytecode_offset);
                    add_type_at_offset(tc, oss, simf->sf, e->type.type,
                        e->type.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE);
                    break;
                }
                case MVM_SPESH_LOG_INVOKE: {
                    MVMSpeshStatsByOffset *oss = by_offset(tc, tss,
                        e->invoke.bytecode_offset);
                    add_invoke_at_offset(tc, oss, simf->sf, e->invoke.sf,
                        e->invoke.caller_is_outer, e->invoke.was_multi);
                    break;
                }
            }
        }

        /* Incorporate callsite type stats (what type tuples did we make a
         * call with). */
        for (i = 0; i < simf->call_type_info_used; i++) {
            MVMSpeshSimCallType *info = &(simf->call_type_info[i]);
            MVMSpeshStatsByOffset *oss = by_offset(tc, tss, info->bytecode_offset);
            add_type_tuple_at_offset(tc, oss, simf->sf, info);
        }

        /* Incorporate OSR hits and bump max depth. */
        if (first_type_hit)
            tss->hits++;
        tss->osr_hits += simf->osr_hits;
        if (frame_depth > tss->max_depth)
            tss->max_depth = frame_depth;

        /* If the callee's last incovation matches the frame just invoked,
         * then log the type tuple against the callsite. */
        if (caller && caller->last_invoke_sf == simf->sf)
            add_sim_call_type_info(tc, caller, caller->last_invoke_offset,
                simf->ss->by_callsite[simf->callsite_idx].cs,
                tss->arg_types);
    }

    /* Clear up offset logs and call type info; they're either incorproated or
     * to be tossed. Also zero OSR hits, so we don't over-inflate them if this
     * frame entry survives. */
    MVM_free(simf->offset_logs);
    simf->offset_logs = NULL;
    simf->offset_logs_used = simf->offset_logs_limit = 0;
    MVM_free(simf->call_type_info);
    simf->call_type_info = NULL;
    simf->call_type_info_used = simf->call_type_info_limit = 0;
    simf->osr_hits = 0;
}

/* Pops the top frame from the sim stack. */
void sim_stack_pop(MVMThreadContext *tc, MVMSpeshSimStack *sims, MVMObject *sf_updated) {
    MVMSpeshSimStackFrame *simf;
    MVMuint32 frame_depth;

    /* Pop off the simulated frame and incorporate logged data into the spesh
     * stats model. */
    if (sims->used == 0)
        MVM_panic(1, "Spesh stats: cannot pop an empty simulation stack");
    sims->used--;
    simf = &(sims->frames[sims->used]);
    frame_depth = sims->depth--;

    /* Incorporate logged data into the statistics model. */
    incorporate_stats(tc, simf, frame_depth,
        sims->used > 0 ? &(sims->frames[sims->used - 1]) : NULL,
        sf_updated);
}

/* Gets the simulation stack frame for the specified correlation ID. If it is
 * not on the top, searches to see if it's further down. If it is, then pops
 * off the top to reach it. If it's not found at all, returns NULL and does
 * nothing to the simulation stack. */
MVMSpeshSimStackFrame * sim_stack_find(MVMThreadContext *tc, MVMSpeshSimStack *sims,
                                       MVMuint32 cid, MVMObject *sf_updated) {
    MVMuint32 found_at = sims->used;
    while (found_at != 0) {
        found_at--;
        if (sims->frames[found_at].cid == cid) {
            MVMint32 pop = (sims->used - found_at) - 1;
            MVMint32 i;
            for (i = 0; i < pop; i++)
                sim_stack_pop(tc, sims, sf_updated);
            return &(sims->frames[found_at]);
        }
    }
    return NULL;
}

/* Pops all the frames in the stack simulation and frees the frames storage. */
void sim_stack_teardown(MVMThreadContext *tc, MVMSpeshSimStack *sims, MVMObject *sf_updated) {
    while (sims->used)
        sim_stack_pop(tc, sims, sf_updated);
    MVM_free(sims->frames);
}

/* Gets the parameter type slot from a simulation frame. */
MVMSpeshStatsType * param_type(MVMThreadContext *tc, MVMSpeshSimStackFrame *simf, MVMSpeshLogEntry *e) {
    if (simf->arg_types) {
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
    }
    return NULL;
}

/* Records a static value for a frame, unless it's already in the log. */
void add_static_value(MVMThreadContext *tc, MVMSpeshSimStackFrame *simf, MVMint32 bytecode_offset,
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
    MVM_ASSIGN_REF(tc, &(simf->sf->body.spesh->common.header), ss->static_values[id].value, value);
}

/* Decides whether to save or free the simulation stack. */
static void save_or_free_sim_stack(MVMThreadContext *tc, MVMSpeshSimStack *sims,
                                   MVMThreadContext *save_on_tc, MVMObject *sf_updated) {
    MVMint32 first_survivor = -1;
    MVMint32 i;
    if (save_on_tc) {
        for (i = 0; i < sims->used; i++) {
            MVMSpeshSimStackFrame *simf = &(sims->frames[i]);
            MVMuint32 age = tc->instance->spesh_stats_version - simf->ss->last_update;
            if (age < MVM_SPESH_STATS_MAX_AGE - 1) {
                first_survivor = i;
                break;
            }
        }
    }
    if (first_survivor >= 0) {
        /* Move survivors to the start. */
        if (first_survivor > 0) {
            sims->used -= first_survivor;
            memmove(sims->frames, sims->frames + first_survivor,
                sims->used * sizeof(MVMSpeshSimStackFrame));
        }

        /* Incorporate data from the rest into the stats model, clearing it
         * away. */
        i = sims->used - 1;
        while (i >= 0) {
            incorporate_stats(tc, &(sims->frames[i]), first_survivor + i,
                i > 0 ? &(sims->frames[i - 1]) : NULL,
                sf_updated);
            i--;
        }

        /* Save frames for next time. */
        save_on_tc->spesh_sim_stack = sims;
    }
    else {
        /* Everything on the simulated stack is too old; throw it away. */
        sim_stack_teardown(tc, sims, sf_updated);
        MVM_free(sims);
    }
}

/* Receives a spesh log and updates static frame statistics. Each static frame
 * that is updated is pushed once into sf_updated. */
void MVM_spesh_stats_update(MVMThreadContext *tc, MVMSpeshLog *sl, MVMObject *sf_updated) {
    MVMuint32 i;
    MVMuint32 n = sl->body.used;
    MVMSpeshSimStack *sims;
    MVMThreadContext *log_from_tc = sl->body.thread->body.tc;
#if MVM_GC_DEBUG
    tc->in_spesh = 1;
#endif
    /* See if we have a simulation stack left over from before; create a new
     * one if not. */
    if (log_from_tc && log_from_tc->spesh_sim_stack) {
        /* Filter out those whose stats pointer is outdated. */
        MVMuint32 insert_pos = 0;
        sims = log_from_tc->spesh_sim_stack;
        for (i = 0; i < sims->used; i++) {
            MVMSpeshStats *cur_stats = sims->frames[i].sf->body.spesh->body.spesh_stats;
            if (cur_stats == sims->frames[i].ss) {
                if (i != insert_pos)
                    sims->frames[insert_pos] = sims->frames[i];
                insert_pos++;
            }
        }
        sims->used = insert_pos;
        log_from_tc->spesh_sim_stack = NULL;
    }
    else {
        sims = MVM_malloc(sizeof(MVMSpeshSimStack));
        sim_stack_init(tc, sims);
    }

    /* Process the log entries. */
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
                sim_stack_push(tc, sims, e->entry.sf, ss, e->id, callsite_idx);
                break;
            }
            case MVM_SPESH_LOG_PARAMETER: {
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf) {
                    MVMSpeshStatsType *type_slot = param_type(tc, simf, e);
                    if (type_slot) {
                        MVM_ASSIGN_REF(tc, &(simf->sf->body.spesh->common.header),
                            type_slot->type, e->param.type);
                        type_slot->type_concrete =
                            e->param.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE ? 1 : 0;
                        type_slot->rw_cont =
                            e->param.flags & MVM_SPESH_LOG_TYPE_FLAG_RW_CONT ? 1 : 0;
                    }
                }
                break;
            }
            case MVM_SPESH_LOG_PARAMETER_DECONT: {
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf) {
                    MVMSpeshStatsType *type_slot = param_type(tc, simf, e);
                    if (type_slot) {
                        MVM_ASSIGN_REF(tc, &(simf->sf->body.spesh->common.header),
                            type_slot->decont_type, e->param.type);
                        type_slot->decont_type_concrete =
                            e->param.flags & MVM_SPESH_LOG_TYPE_FLAG_CONCRETE;
                    }
                }
                break;
            }
            case MVM_SPESH_LOG_TYPE:
            case MVM_SPESH_LOG_INVOKE: {
                /* We only incorporate these into the model later, and only
                 * then if we need to. For now, just keep references to
                 * them. */
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf) {
                    if (simf->offset_logs_used == simf->offset_logs_limit) {
                        simf->offset_logs_limit += 32;
                        simf->offset_logs = MVM_realloc(simf->offset_logs,
                            simf->offset_logs_limit * sizeof(MVMSpeshLogEntry *));
                    }
                    simf->offset_logs[simf->offset_logs_used++] = e;
                    if (e->kind == MVM_SPESH_LOG_INVOKE) {
                        simf->last_invoke_offset = e->invoke.bytecode_offset;
                        simf->last_invoke_sf = e->invoke.sf;
                    }
                }
                break;
            }
            case MVM_SPESH_LOG_OSR: {
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf)
                    simf->osr_hits++;
                break;
            }
            case MVM_SPESH_LOG_STATIC: {
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf)
                    add_static_value(tc, simf, e->value.bytecode_offset, e->value.value);
                break;
            }
            case MVM_SPESH_LOG_RETURN: {
                MVMSpeshSimStackFrame *simf = sim_stack_find(tc, sims, e->id, sf_updated);
                if (simf) {
                    MVMStaticFrame *called_sf = simf->sf;
                    sim_stack_pop(tc, sims, sf_updated);
                    if (e->type.type && sims->used) {
                        MVMSpeshSimStackFrame *caller = &(sims->frames[sims->used - 1]);
                        if (called_sf == caller->last_invoke_sf) {
                            if (caller->offset_logs_used == caller->offset_logs_limit) {
                                caller->offset_logs_limit += 32;
                                caller->offset_logs = MVM_realloc(caller->offset_logs,
                                    caller->offset_logs_limit * sizeof(MVMSpeshLogEntry *));
                            }
                            e->type.bytecode_offset = caller->last_invoke_offset;
                            caller->offset_logs[caller->offset_logs_used++] = e;
                        }
                    }
                }
                break;
            }
        }
    }
    save_or_free_sim_stack(tc, sims, log_from_tc, sf_updated);
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
        MVMStaticFrameSpesh *spesh = sf->body.spesh;
        MVMSpeshStats *ss = spesh->body.spesh_stats;
        if (!ss) {
            /* No stats; already destroyed, don't keep this frame under
             * consideration. */
        }
        else if (tc->instance->spesh_stats_version - ss->last_update > MVM_SPESH_STATS_MAX_AGE) {
            MVM_spesh_stats_destroy(tc, ss);
            spesh->body.spesh_stats = NULL;
        }
        else {
            MVM_repr_bind_pos_o(tc, check_frames, insert_pos++, (MVMObject *)sf);
        }
    }
    MVM_repr_pos_set_elems(tc, check_frames, insert_pos);
}

void MVM_spesh_stats_gc_mark(MVMThreadContext *tc, MVMSpeshStats *ss, MVMGCWorklist *worklist) {
    if (ss) {
        MVMuint32 i, j, k, l, m;
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
                    for (l = 0; l < by_offset->num_invokes; l++)
                        MVM_gc_worklist_add(tc, worklist, &(by_offset->invokes[l].sf));
                    for (l = 0; l < by_offset->num_type_tuples; l++) {
                        MVMSpeshStatsType *off_types = by_offset->type_tuples[l].arg_types;
                        MVMuint32 num_off_types = by_offset->type_tuples[l].cs->flag_count;
                        for (m = 0; m < num_off_types; m++) {
                            MVM_gc_worklist_add(tc, worklist, &(off_types[m].type));
                            MVM_gc_worklist_add(tc, worklist, &(off_types[m].decont_type));
                        }
                    }
                }
            }
        }
        for (i = 0; i < ss->num_static_values; i++)
            MVM_gc_worklist_add(tc, worklist, &(ss->static_values[i].value));
    }
}

void MVM_spesh_stats_destroy(MVMThreadContext *tc, MVMSpeshStats *ss) {
    if (ss) {
        MVMuint32 i, j, k, l;
        for (i = 0; i < ss->num_by_callsite; i++) {
            MVMSpeshStatsByCallsite *by_cs = &(ss->by_callsite[i]);
            for (j = 0; j < by_cs->num_by_type; j++) {
                MVMSpeshStatsByType *by_type = &(by_cs->by_type[j]);
                for (k = 0; k < by_type->num_by_offset; k++) {
                    MVMSpeshStatsByOffset *by_offset = &(by_type->by_offset[k]);
                    MVM_free(by_offset->types);
                    MVM_free(by_offset->invokes);
                    for (l = 0; l < by_offset->num_type_tuples; l++)
                        MVM_free(by_offset->type_tuples[l].arg_types);
                    MVM_free(by_offset->type_tuples);
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

void MVM_spesh_sim_stack_gc_mark(MVMThreadContext *tc, MVMSpeshSimStack *sims,
                                 MVMGCWorklist *worklist) {
    MVMuint32 n = sims ? sims->used : 0;
    MVMuint32 i;
    for (i = 0; i < n; i++) {
        MVMSpeshSimStackFrame *simf = &(sims->frames[i]);
        MVM_gc_worklist_add(tc, worklist, &(simf->sf));
        MVM_gc_worklist_add(tc, worklist, &(simf->last_invoke_sf));
    }
}

void MVM_spesh_sim_stack_destroy(MVMThreadContext *tc, MVMSpeshSimStack *sims) {
    if (sims) {
        MVM_free(sims->frames);
        MVM_free(sims);
    }
}
