#include "moar.h"

/* Provided spesh is enabled, set up specialization data logging for the
 * current thread. */
void MVM_spesh_log_initialize_thread(MVMThreadContext *tc, MVMint32 main_thread) {
    if (tc->instance->spesh_enabled) {
        tc->spesh_log = MVM_spesh_log_create(tc, tc->thread_obj);
        tc->spesh_log_quota = main_thread
            ? MVM_SPESH_LOG_QUOTA_MAIN_THREAD
            : MVM_SPESH_LOG_QUOTA;
    }
}

/* Creates a spesh log for the specified target thread. */
MVMSpeshLog * MVM_spesh_log_create(MVMThreadContext *tc, MVMThread *target_thread) {
    MVMSpeshLog *result;
    MVMROOT(tc, target_thread, {
        result = (MVMSpeshLog *)MVM_repr_alloc_init(tc, tc->instance->SpeshLog);
        MVM_ASSIGN_REF(tc, &(result->common.header), result->body.thread, target_thread);
    });
    return result;
}

/* Increments the used count and - if it hits the limit - sends the log off
 * to the worker thread and NULLs it out. */
void send_log(MVMThreadContext *tc, MVMSpeshLog *sl) {
    if (tc->instance->spesh_blocking) {
        sl->body.block_mutex = MVM_malloc(sizeof(uv_mutex_t));
        uv_mutex_init(sl->body.block_mutex);
        sl->body.block_condvar = MVM_malloc(sizeof(uv_cond_t));
        uv_cond_init(sl->body.block_condvar);
        uv_mutex_lock(sl->body.block_mutex);
        MVMROOT(tc, sl, {
            MVM_repr_push_o(tc, tc->instance->spesh_queue, (MVMObject *)sl);
            MVM_gc_mark_thread_blocked(tc);
            while (!MVM_load(&(sl->body.completed)))
                uv_cond_wait(sl->body.block_condvar, sl->body.block_mutex);
            MVM_gc_mark_thread_unblocked(tc);
        });
        uv_mutex_unlock(sl->body.block_mutex);
    }
    else {
        MVM_repr_push_o(tc, tc->instance->spesh_queue, (MVMObject *)sl);
    }
    if (MVM_decr(&(tc->spesh_log_quota)) > 1) {
        tc->spesh_log = MVM_spesh_log_create(tc, tc->thread_obj);
    }
    else {
        MVM_telemetry_timestamp(tc, "ran out of spesh log quota");
        tc->spesh_log = NULL;
    }
}
void commit_entry(MVMThreadContext *tc, MVMSpeshLog *sl) {
    sl->body.used++;
    if (sl->body.used == sl->body.limit)
        send_log(tc, sl);
}

/* Handles the case where we enter a new compilation unit and have either no
 * spesh log or a spesh log that's quite full. This might hinder us in getting
 * enough data recorded for a tight outer loop in a benchmark. Either grant a
 * bonus log or send the log early so we can have a fresh one. */
void MVM_spesh_log_new_compunit(MVMThreadContext *tc) {
    if (tc->num_compunit_extra_logs++ < 5) {
        if (tc->spesh_log)
            if (tc->spesh_log->body.used > tc->spesh_log->body.limit / 4)
                send_log(tc, tc->spesh_log);
        if (!tc->spesh_log) {
            if (MVM_incr(&(tc->spesh_log_quota)) == 0) {
                tc->spesh_log = MVM_spesh_log_create(tc, tc->thread_obj);
                tc->spesh_log->body.was_compunit_bumped = 1;
            }
        }
    }
}

/* Log the entry to a call frame. */
void MVM_spesh_log_entry(MVMThreadContext *tc, MVMint32 cid, MVMStaticFrame *sf, MVMCallsite *cs) {
    MVMSpeshLog *sl = tc->spesh_log;
    if (sl) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_ENTRY;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->entry.sf, sf);
        entry->entry.cs = cs->is_interned ? cs : NULL;
        commit_entry(tc, sl);
    }
}

/* Log an OSR point being hit. */
void MVM_spesh_log_osr(MVMThreadContext *tc) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = MVM_SPESH_LOG_OSR;
    entry->id = cid;
    entry->osr.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
    commit_entry(tc, sl);
}

/* Log a type. */
void MVM_spesh_log_type(MVMThreadContext *tc, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = MVM_SPESH_LOG_TYPE;
    entry->id = cid;
    MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
    entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
    entry->type.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
    commit_entry(tc, sl);
}

/* Log a parameter type and, maybe, decontainerized type. */
static void log_param_type(MVMThreadContext *tc, MVMint32 cid, MVMuint16 arg_idx,
                           MVMObject *value, MVMSpeshLogEntryKind kind, MVMint32 rw_cont) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = kind;
    entry->id = cid;
    MVM_ASSIGN_REF(tc, &(sl->common.header), entry->param.type, value->st->WHAT);
    entry->param.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
    if (rw_cont)
        entry->param.flags |= MVM_SPESH_LOG_TYPE_FLAG_RW_CONT;
    entry->param.arg_idx = arg_idx;
    commit_entry(tc, sl);
}
void MVM_spesh_log_parameter(MVMThreadContext *tc, MVMuint16 arg_idx, MVMObject *param) {
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMContainerSpec const *cs = STABLE(param)->container_spec;
    MVMROOT(tc, param, {
        log_param_type(tc, cid, arg_idx, param, MVM_SPESH_LOG_PARAMETER,
            cs && IS_CONCRETE(param) && cs->fetch_never_invokes
                ? cs->can_store(tc, param)
                : 0);
    });
    if (tc->spesh_log && IS_CONCRETE(param)) {
        if (cs && cs->fetch_never_invokes) {
            MVMRegister r;
            cs->fetch(tc, param, &r);
            log_param_type(tc, cid, arg_idx, r.o, MVM_SPESH_LOG_PARAMETER_DECONT, 0);
        }
    }
}

/* Log a static value. */
void MVM_spesh_log_static(MVMThreadContext *tc, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = MVM_SPESH_LOG_STATIC;
    entry->id = cid;
    MVM_ASSIGN_REF(tc, &(sl->common.header), entry->value.value, value);
    entry->value.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
    commit_entry(tc, sl);
}

/* Log a decont, only those that did not invoke. */
void MVM_spesh_log_decont(MVMThreadContext *tc, MVMuint8 *prev_op, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    if (prev_op - 4 == *(tc->interp_cur_op)) {
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_TYPE;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
        entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
        entry->type.bytecode_offset = (prev_op - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}

/* Log the target of an invocation; we log the static frame and whether the
 * outer of the code object is the current frame. */
void MVM_spesh_log_invoke_target(MVMThreadContext *tc, MVMObject *invoke_target,
                                 MVMint16 was_multi) {
    if (REPR(invoke_target)->ID == MVM_REPR_ID_MVMCode && IS_CONCRETE(invoke_target)) {
        MVMCode *invoke_code = (MVMCode *)invoke_target;
        MVMSpeshLog *sl = tc->spesh_log;
        MVMint32 cid = tc->cur_frame->spesh_correlation_id;
        MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
        entry->kind = MVM_SPESH_LOG_INVOKE;
        entry->id = cid;
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->invoke.sf, invoke_code->body.sf);
        entry->invoke.caller_is_outer = invoke_code->body.outer == tc->cur_frame;
        entry->invoke.was_multi = was_multi;
        entry->invoke.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
        commit_entry(tc, sl);
    }
}

/* Log the type returned to a frame after an invocation. */
void MVM_spesh_log_return_type(MVMThreadContext *tc, MVMObject *value) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = MVM_SPESH_LOG_RETURN;
    entry->id = cid;
    if (value) {
        MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
        entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
    }
    else {
        entry->type.type = NULL;
        entry->type.flags = 0;
    }
    entry->type.bytecode_offset = 0; /* Not relevant for this case. */
    commit_entry(tc, sl);
}
