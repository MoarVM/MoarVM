/* Option to enable checking of define/use chains for debugging purposes. */
#define MVM_SPESH_CHECK_DU 0

/* Usage information, which is held per SSA written register. */
struct MVMSpeshUsages {
    /* The normal use chain entries. */
    MVMSpeshUseChainEntry *users;

    /* The deopt use chain entries. */
    MVMSpeshDeoptUseEntry *deopt_users;

    /* Does the instruction need to be preserved as it is setting an exception
     * handler block register? */
    MVMuint8 handler_required;

    /* Has the writer been processed while doing deopt use analysis? */
    MVMuint8 deopt_write_processed;

#if MVM_SPESH_CHECK_DU
    /* Is the writer in the graph? */
    MVMuint8 writer_seen_in_graph;
#endif
};

/* Linked list of using instructions. */
struct MVMSpeshUseChainEntry {
    /* The reading instruction. */
    MVMSpeshIns *user;

    /* The next entry in the chain. */
    MVMSpeshUseChainEntry *next;

    /* Has this read been processed while doing deopt use analysis? */
    MVMuint8 deopt_read_processed;

#if MVM_SPESH_CHECK_DU
    /* Used in debugging. */
    MVMuint8 seen_in_graph;
#endif
};

/* A use for the purposes of deoptimization. */
struct MVMSpeshDeoptUseEntry {
    /* Index that was used, or -1 if unconditional requirement. */
    MVMint32 deopt_idx;

    /* The next entry in the chain. */
    MVMSpeshDeoptUseEntry *next;
};

void MVM_spesh_usages_add(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by);
void MVM_spesh_usages_add_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by);
void MVM_spesh_usages_delete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by);
void MVM_spesh_usages_delete_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by);
void MVM_spesh_usages_add_for_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts);
void MVM_spesh_usages_add_for_handler_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used);
void MVM_spesh_usages_create_usage(MVMThreadContext *tc, MVMSpeshGraph *g);
void MVM_spesh_usages_create_deopt_usage(MVMThreadContext *tc, MVMSpeshGraph *g);
void MVM_spesh_usages_add_deopt_usage(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshFacts *facts, MVMint32 deopt_idx);
void MVM_spesh_usages_add_deopt_usage_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshOperand operand, MVMint32 deopt_idx);
void MVM_spesh_usages_add_unconditional_deopt_usage(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts);
void MVM_spesh_usages_add_unconditional_deopt_usage_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMSpeshOperand operand);
void MVM_spesh_usages_retain_deopt_index(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint32 idx);
void MVM_spesh_usages_remove_unused_deopt(MVMThreadContext *tc, MVMSpeshGraph *g);
MVMuint32 MVM_spesh_usages_is_used(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_is_used_by_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_is_used_by_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_used_once(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_count(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
#if MVM_SPESH_CHECK_DU
void MVM_spesh_usages_check(MVMThreadContext *tc, MVMSpeshGraph *g);
#endif
