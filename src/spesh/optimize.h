/* Maximum args a call can take for us to consider it for optimization. */
#define MAX_ARGS_FOR_OPT    4

/* Information we've gathered about the current call we're optimizing, and the
 * arguments it will take. */
struct MVMSpeshCallInfo {
    MVMCallsite   *cs;
    MVMint8        arg_is_const[MAX_ARGS_FOR_OPT];
    MVMSpeshFacts *arg_facts[MAX_ARGS_FOR_OPT];
    MVMSpeshIns   *prepargs_ins;
    MVMSpeshBB    *prepargs_bb;
    MVMSpeshIns   *arg_ins[MAX_ARGS_FOR_OPT];
};

void MVM_spesh_optimize(MVMThreadContext *tc, MVMSpeshGraph *g);
MVM_PUBLIC MVMint16 MVM_spesh_add_spesh_slot(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCollectable *c);
MVM_PUBLIC MVMSpeshFacts * MVM_spesh_get_and_use_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o);
MVM_PUBLIC MVMSpeshFacts * MVM_spesh_get_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o);
MVM_PUBLIC void MVM_spesh_use_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *f);
MVM_PUBLIC MVMString * MVM_spesh_get_string(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand o);
