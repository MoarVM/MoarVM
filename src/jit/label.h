MVMint32 MVM_jit_label_before_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg);
MVMint32 MVM_jit_label_after_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg);
MVMint32 MVM_jit_label_before_bb(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb);
MVMint32 MVM_jit_label_before_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins);
MVMint32 MVM_jit_label_after_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins);
