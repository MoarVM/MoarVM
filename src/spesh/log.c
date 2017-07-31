#include "moar.h"

/* Provided spesh is enabled, set up specialization data logging for the
 * current thread. */
void MVM_spesh_log_initialize_thread(MVMThreadContext *tc) {
    if (tc->instance->spesh_enabled) {
        tc->spesh_log = MVM_spesh_log_create(tc, tc->thread_obj);
        tc->spesh_log_quota = MVM_SPESH_LOG_QUOTA;
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
void commit_entry(MVMThreadContext *tc, MVMSpeshLog *sl) {
    sl->body.used++;
    if (sl->body.used == sl->body.limit) {
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
        if (MVM_decr(&(tc->spesh_log_quota)) > 1)
            tc->spesh_log = MVM_spesh_log_create(tc, tc->thread_obj);
        else {
            MVM_telemetry_timestamp(tc, "ran out of spesh log quota");
            tc->spesh_log = NULL;
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
                           MVMObject *value, MVMSpeshLogEntryKind kind) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = kind;
    entry->id = cid;
    MVM_ASSIGN_REF(tc, &(sl->common.header), entry->param.type, value->st->WHAT);
    entry->param.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
    entry->param.arg_idx = arg_idx;
    commit_entry(tc, sl);
}
void MVM_spesh_log_parameter(MVMThreadContext *tc, MVMuint16 arg_idx, MVMObject *param) {
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMROOT(tc, param, {
        log_param_type(tc, cid, arg_idx, param, MVM_SPESH_LOG_PARAMETER);
    });
    if (IS_CONCRETE(param)) {
        MVMContainerSpec const *cs = STABLE(param)->container_spec;
        if (cs && cs->fetch_never_invokes) {
            MVMRegister r;
            cs->fetch(tc, param, &r);
            log_param_type(tc, cid, arg_idx, r.o, MVM_SPESH_LOG_PARAMETER_DECONT);
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

/* Log the target of an invocation. */
void MVM_spesh_log_invoke_target(MVMThreadContext *tc, MVMObject *invoke_target) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = tc->cur_frame->spesh_correlation_id;
    MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
    entry->kind = MVM_SPESH_LOG_INVOKE;
    entry->id = cid;
    MVM_ASSIGN_REF(tc, &(sl->common.header), entry->value.value, invoke_target);
    entry->value.bytecode_offset = (*(tc->interp_cur_op) - *(tc->interp_bytecode_start)) - 2;
    commit_entry(tc, sl);
}

/* Log the type returned to a frame after an invocation. */
void MVM_spesh_log_return_type(MVMThreadContext *tc, MVMFrame *target) {
    MVMSpeshLog *sl = tc->spesh_log;
    MVMint32 cid = target->spesh_correlation_id;
    if (sl && cid) {
        MVMObject *value = target->return_value->o;
        if (value) {
            MVMSpeshLogEntry *entry = &(sl->body.entries[sl->body.used]);
            entry->kind = MVM_SPESH_LOG_TYPE;
            entry->id = cid;
            MVM_ASSIGN_REF(tc, &(sl->common.header), entry->type.type, value->st->WHAT);
            entry->type.flags = IS_CONCRETE(value) ? MVM_SPESH_LOG_TYPE_FLAG_CONCRETE : 0;
            entry->type.bytecode_offset =
                (target->return_address - MVM_frame_effective_bytecode(target))
                - 6; /* 6 is the length of the invoke_o opcode and operands */
            commit_entry(tc, sl);
        }
    }
}
