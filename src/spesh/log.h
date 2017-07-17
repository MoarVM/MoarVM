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
#define MVM_SPESH_LOG_DEFAULT_ENTRIES 4096

/* The number of logged invocations before we decide we've enough data for
 * the time being. */
#define MVM_SPESH_LOG_LOGGED_ENOUGH 100

void MVM_spesh_log_create_for_thread(MVMThreadContext *tc);
void MVM_spesh_log_entry(MVMThreadContext *tc, MVMint32 cid, MVMStaticFrame *sf, MVMCallsite *cs);
void MVM_spesh_log_osr(MVMThreadContext *tc);
void MVM_spesh_log_parameter(MVMThreadContext *tc, MVMuint16 arg_idx, MVMObject *param);
void MVM_spesh_log_type(MVMThreadContext *tc, MVMObject *value);
void MVM_spesh_log_static(MVMThreadContext *tc, MVMObject *value);
void MVM_spesh_log_decont(MVMThreadContext *tc, MVMuint8 *prev_op, MVMObject *value);
void MVM_spesh_log_invoke_target(MVMThreadContext *tc, MVMObject *invoke_target);
void MVM_spesh_log_return_type(MVMThreadContext *tc, MVMFrame *target);
