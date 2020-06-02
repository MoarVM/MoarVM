/* Information about an inserted guard instruction due to logging. */
struct MVMSpeshLogGuard {
    /* Instruction and containing basic block. */
    MVMSpeshIns *ins;
    MVMSpeshBB  *bb;

    /* Have we made use of the gurad? */
    MVMuint32 used;
};

/* The default number of entries collected into a thread's spesh log buffer
 * before it is sent to a specialization worker. */
#define MVM_SPESH_LOG_DEFAULT_ENTRIES 16384

/* The number of spesh log buffers a thread can write before the spesh worker
 * thread allows it to write more (effectively, the limit on the number of
 * outstanding work per thread). Threads other than the main one getting a
 * bit less buffer space helps reduce memory use a bit. */
#define MVM_SPESH_LOG_QUOTA_MAIN_THREAD 3
#define MVM_SPESH_LOG_QUOTA 2

/* The number of logged invocations before we decide we've enough data for
 * the time being; should be at least the maximum threshold value in
 * thresholds.c, but we set it higher to allow more data collection. */
#define MVM_SPESH_LOG_LOGGED_ENOUGH 1000

/* Quick inline checks if we are logging, to save function call overhead. */
MVM_STATIC_INLINE MVMint32 MVM_spesh_log_is_logging(MVMThreadContext *tc) {
    MVMFrame *cur_frame = tc->cur_frame;
    return cur_frame->spesh_cand == NULL && cur_frame->spesh_correlation_id && tc->spesh_log;
}
MVM_STATIC_INLINE MVMint32 MVM_spesh_log_is_caller_logging(MVMThreadContext *tc) {
    MVMFrame *caller_frame = tc->cur_frame->caller;
    return caller_frame && caller_frame->spesh_cand == NULL &&
        caller_frame->spesh_correlation_id && tc->spesh_log;
}

void MVM_spesh_log_initialize_thread(MVMThreadContext *tc, MVMint32 main_thread);
MVMSpeshLog * MVM_spesh_log_create(MVMThreadContext *tc, MVMThread *target_thread);
void MVM_spesh_log_new_compunit(MVMThreadContext *tc);
void MVM_spesh_log_entry(MVMThreadContext *tc, MVMint32 cid, MVMStaticFrame *sf,
        MVMCallsite *cs, MVMRegister *args);
void MVM_spesh_log_osr(MVMThreadContext *tc);
void MVM_spesh_log_type(MVMThreadContext *tc, MVMObject *value);
void MVM_spesh_log_decont(MVMThreadContext *tc, MVMuint8 *prev_op, MVMObject *value);
void MVM_spesh_log_invoke_target(MVMThreadContext *tc, MVMObject *invoke_target,
    MVMuint16 was_multi);
void MVM_spesh_log_return_type(MVMThreadContext *tc, MVMObject *value);
void MVM_spesh_log_return_to_unlogged(MVMThreadContext *tc);
void MVM_spesh_log_plugin_resolution(MVMThreadContext *tc, MVMuint32 bytecode_offset,
        MVMuint16 guard_index);
void MVM_spesh_log_dispatch_resolution(MVMThreadContext *tc, MVMuint32 bytecode_offset,
        MVMuint16 guard_index);
