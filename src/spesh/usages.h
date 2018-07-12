/* Option to enable checking of define/use chains for debugging purposes. */
#define MVM_SPESH_CHECK_DU 0

/* Usage information, which is held per SSA written register. */
struct MVMSpeshUsages {
    /* The use chain entries. */
    MVMSpeshUseChainEntry *users;

    /* Does the instruction need to be preserved for the sake of deopt? */
    MVMuint8 deopt_required;

    /* Does the instruction need to be preserved as it is setting an exception
     * handler block register? */
    MVMuint8 handler_required;

#if MVM_SPESH_CHECK_DU
    /* Is the writer in the graph? */
    MVMuint8 writer_seen_in_graph;
#endif
};

/* Linked list of using instructions. */
struct MVMSpeshUseChainEntry {
    MVMSpeshIns *user;
    MVMSpeshUseChainEntry *next;
#if MVM_SPESH_CHECK_DU
    MVMuint8 seen_in_graph;
#endif
};

void MVM_spesh_usages_add(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by);
void MVM_spesh_usages_add_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by);
void MVM_spesh_usages_delete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by);
void MVM_spesh_usages_delete_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by);
void MVM_spesh_usages_add_for_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts);
void MVM_spesh_usages_add_for_deopt_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used);
void MVM_spesh_usages_clear_for_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts);
void MVM_spesh_usages_clear_for_deopt_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand unused);
void MVM_spesh_usages_add_for_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts);
void MVM_spesh_usages_add_for_handler_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used);
MVMuint32 MVM_spesh_usages_is_used(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_is_used_by_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_is_used_by_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_used_once(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
MVMuint32 MVM_spesh_usages_count(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check);
#if MVM_SPESH_CHECK_DU
void MVM_spesh_usages_check(MVMThreadContext *tc, MVMSpeshGraph *g);
#endif
