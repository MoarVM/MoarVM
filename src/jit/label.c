#include "moar.h"

MVMint32 MVM_jit_label_before_bb(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb) {
    return bb->idx;
}

MVMint32 MVM_jit_label_before_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg) {
    return 0;
}

MVMint32 MVM_jit_label_after_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshGraph *sg) {
    /* Larger than all basic block labels */
    return sg->num_bbs;
}


MVMint32 MVM_jit_label_before_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* PHI nodes are always at the start of a basic block, so this not is really necessary */
    while (ins->prev && ins->prev->info->opcode == MVM_SSA_PHI) {
        ins = ins->prev;
    }
    /* Without predecessor instructions this is the same as the basic block */
    if (!ins->prev) {
        return MVM_jit_label_before_bb(tc, jg, bb);
    }
    /* Requires a separate label */
    return MVM_jit_label_for_obj(tc, jg, ins);
}

MVMint32 MVM_jit_label_after_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* A label after this instruction equals a label before the next */
    if (ins->next) {
        return MVM_jit_label_before_ins(tc, jg, bb, ins->next);
    }
    /* Or before the next basic block */
    if (bb->linear_next) {
        return MVM_jit_label_before_bb(tc, jg, bb->linear_next);
    }
    /* And in the edge case after the graph */
    return MVM_jit_label_after_graph(tc, jg, jg->sg);
}

MVMint32 MVM_jit_label_for_obj(MVMThreadContext *tc, MVMJitGraph *jg, void *obj) {
    MVMint32 i;
    /* Reverse search; it is pretty likely we've seen this ins just before */
    i      = jg->obj_labels_num;
    while (i--) {
        if (jg->obj_labels[i] == obj) {
            return i + jg->obj_label_ofs;
        }
    }
    /* Add and return new instruction label */
    MVM_VECTOR_PUSH(jg->obj_labels, obj);
    return jg->obj_labels_num - 1 + jg->obj_label_ofs;
}

MVMint32 MVM_jit_label_is_for_graph(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label) {
    return label == 0 || label == jg->sg->num_bbs;
}

MVMint32 MVM_jit_label_is_for_bb(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label) {
    return label > 0 && label < jg->sg->num_bbs;
}

MVMint32 MVM_jit_label_is_for_ins(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label) {
    return label > jg->sg->num_bbs && label <= jg->sg->num_bbs + jg->obj_labels_num;
}

MVMint32 MVM_jit_label_is_internal(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label) {
    /* WARNING: This is *NOT VALID* during jit graph building */
    return label >= jg->num_labels;
}
