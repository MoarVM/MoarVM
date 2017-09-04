/* Aqcuiqre labels */
MVMint32 MVM_jit_label_before_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg);
MVMint32 MVM_jit_label_after_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg);
MVMint32 MVM_jit_label_before_bb(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb);
MVMint32 MVM_jit_label_before_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins);
MVMint32 MVM_jit_label_after_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins);
/* GIANT HACK UNFORTUNATELY */
MVMint32 MVM_jit_label_for_obj(MVMThreadContext *tc, MVMJitGraph *jg, void *obj);

/* Test label category */
MVMint32 MVM_jit_label_is_for_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label);
MVMint32 MVM_jit_label_is_for_bb(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label);
MVMint32 MVM_jit_label_is_for_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label);

/* Internal labels aren't actually assigned their 'final number' before
 * compilation, so this is *NOT VALID* during graph building */
MVMint32 MVM_jit_label_is_internal(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label);
