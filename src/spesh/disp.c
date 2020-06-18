#include "moar.h"

/* Create op info for a dispatch instruction, so that during specialization we
 * can pretend it's not varargs. */
MVMOpInfo * MVM_spesh_disp_create_dispatch_op_info(MVMThreadContext *tc, MVMSpeshGraph *g,
        const MVMOpInfo *base_info, MVMCallsite *callsite) {
    /* In general, ops support up to an operand limit; in the case that there are more,
     * we'd overrun the buffer. We thus allocate more. */
    MVMuint32 total_ops = base_info->num_operands + callsite->flag_count;
    size_t total_size = sizeof(MVMOpInfo) + (total_ops > MVM_MAX_OPERANDS
            ? total_ops - MVM_MAX_OPERANDS
            : 0);
    MVMOpInfo *dispatch_info = MVM_spesh_alloc(tc, g, total_size);

    /* Populate based on the original operation. */
    memcpy(dispatch_info, base_info, sizeof(MVMOpInfo));

    /* Tweak the operand count and set up new operand info based on the callsite. */
    dispatch_info->num_operands += callsite->flag_count;
    MVMuint16 operand_index = base_info->num_operands;
    MVMuint16 flag_index;
    for (flag_index = 0; flag_index < callsite->flag_count; operand_index++, flag_index++) {
        MVMCallsiteFlags flag = callsite->arg_flags[flag_index];
        if (flag & MVM_CALLSITE_ARG_OBJ) {
            dispatch_info->operands[operand_index] = MVM_operand_obj;
        }
        else if (flag & MVM_CALLSITE_ARG_INT) {
            dispatch_info->operands[operand_index] = MVM_operand_int64;
        }
        else if (flag & MVM_CALLSITE_ARG_NUM) {
            dispatch_info->operands[operand_index] = MVM_operand_num64;
        }
        else if (flag & MVM_CALLSITE_ARG_STR) {
            dispatch_info->operands[operand_index] = MVM_operand_str;
        }
        dispatch_info->operands[operand_index] |= MVM_operand_read_reg;
    }

    return dispatch_info;
}

/* Hit count of an outcome, used for analizing how to optimize the dispatch. */
typedef struct {
    MVMuint32 outcome;
    MVMuint32 hits;
} OutcomeHitCount;

/* Rewrite a dispatch instruction into an sp_dispatch one, resolving only the
 * static frame and inline cache offset. */
static void rewrite_to_sp_dispatch(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset) {
    /* Resolve the callsite. */
    MVMuint32 callsite_idx = ins->operands[ins->info->opcode == MVM_OP_dispatch_v ? 1 : 2].callsite_idx;
    MVMCallsite *callsite = g->sf->body.cu->body.callsites[callsite_idx];

    /* Pick the new dispatch instruction and create instruction info for it. */
    const MVMOpInfo *new_ins_base_info;
    switch (ins->info->opcode) {
        case MVM_OP_dispatch_v: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_v); break;
        case MVM_OP_dispatch_i: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_i); break;
        case MVM_OP_dispatch_n: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_n); break;
        case MVM_OP_dispatch_o: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_o); break;
        case MVM_OP_dispatch_s: new_ins_base_info = MVM_op_get_op(MVM_OP_sp_dispatch_s); break;
        default:
            MVM_oops(tc, "Unexpected dispatch instruction to rewrite");
    }
    MVMOpInfo *new_ins_info = MVM_spesh_disp_create_dispatch_op_info(tc, g,
            new_ins_base_info, callsite);
    ins->info = new_ins_info;

    /* Rewrite the operands. */
    MVMSpeshOperand *new_operands = MVM_spesh_alloc(tc, g,
            new_ins_info->num_operands * sizeof(MVMSpeshOperand));
    MVMuint32 target = 0;
    MVMuint32 source = 0;
    if (new_ins_info->opcode != MVM_OP_sp_dispatch_v)
        new_operands[target++] = ins->operands[source++]; /* Result value */
    new_operands[target++] = ins->operands[source++]; /* Dispatcher name */
    new_operands[target++] = ins->operands[source++]; /* Callsite index */
    new_operands[target++].lit_i16 = MVM_spesh_add_spesh_slot_try_reuse(tc, g,
            (MVMCollectable *)g->sf);
    new_operands[target++].lit_ui32 = MVM_disp_inline_cache_get_slot(tc, g->sf, bytecode_offset);
    memcpy(new_operands + target, ins->operands + source,
            callsite->flag_count * sizeof(MVMSpeshOperand));
    ins->operands = new_operands;
}

/* Rewrite an unhit dispatch instruction. */
static void rewrite_unhit(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset) {
    MVM_spesh_graph_add_comment(tc, g, ins, "Never dispatched");
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
}

/* Rewrite a dispatch instruction that is considered monomorphic. */
static void rewrite_monomorphic(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset, MVMuint32 outcome) {
    // TODO
    MVM_spesh_graph_add_comment(tc, g, ins, "Deemed monomorphic (outcome %d)", outcome);
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
}

/* Rewrite a dispatch instruction that is polymorphic or megamorphic. */
static void rewrite_polymorphic(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins,
        MVMuint32 bytecode_offset, OutcomeHitCount *outcomes, MVMuint32 num_outcomes) {
    // TODO
    MVM_spesh_graph_add_comment(tc, g, ins, "Deemed polymorphic");
    rewrite_to_sp_dispatch(tc, g, ins, bytecode_offset);
}

/* Drives the overall process of optimizing a dispatch instruction. The instruction
 * will always recieve some transformation, even if it's simply to sp_dispatch_*,
 * which pre-resolves the inline cache (and so allows inlining of code that still
 * wants to reference the inline cache of the place it came from). */
static int compare_hits(const void *a, const void *b) {
    return ((OutcomeHitCount *)b)->hits - ((OutcomeHitCount *)a)->hits;
}
void MVM_spesh_disp_optimize(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshPlanned *p,
        MVMSpeshIns *ins) {
    /* Locate the inline cache bytecode offset. There must always be one. */
    MVMSpeshAnn *ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_CACHED)
            break;
        ann = ann->next;
    }
    if (!ann)
        MVM_oops(tc, "Dispatch specialization could not find bytecode offset for dispatch instruction");
    MVMuint32 bytecode_offset = ann->data.bytecode_offset;

    /* Find any statistics we have on what outcomes were selected by the
     * dispatcher and aggregate them. */
    MVM_VECTOR_DECL(OutcomeHitCount, outcome_hits);
    MVM_VECTOR_INIT(outcome_hits, 0);
    MVMuint32 total_hits = 0;
    MVMuint32 i;
    for (i = 0; i < (p ? p->num_type_stats : 0); i++) {
        MVMSpeshStatsByType *ts = p->type_stats[i];
        MVMuint32 j;
        for (j = 0; j < ts->num_by_offset; j++) {
            if (ts->by_offset[j].bytecode_offset == bytecode_offset) {
                /* We found some stats at the offset of the dispatch. Count the
                 * hits. */
                MVMuint32 k;
                for (k = 0; k < ts->by_offset[j].num_plugin_guards; k++) {
                    MVMSpeshStatsPluginGuardCount *outcome_count =
                            &(ts->by_offset[j].plugin_guards[k]);
                    MVMuint32 l;
                    MVMuint32 found = 0;
                    for (l = 0; l < MVM_VECTOR_ELEMS(outcome_hits); l++) {
                        if (outcome_hits[l].outcome == outcome_count->guard_index) {
                            outcome_hits[l].hits += outcome_count->count;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        OutcomeHitCount ohc = {
                            .outcome = outcome_count->guard_index,
                            .hits = outcome_count->count
                        };
                        MVM_VECTOR_PUSH(outcome_hits, ohc);
                    }
                    total_hits += outcome_count->count;
                }
                break;
            }
        }
    }
    qsort(outcome_hits, MVM_VECTOR_ELEMS(outcome_hits), sizeof(OutcomeHitCount), compare_hits);

    /* If there are no hits, we can only rewrite it to sp_dispatch. */
    if (MVM_VECTOR_ELEMS(outcome_hits) == 0)
        rewrite_unhit(tc, g, ins, bytecode_offset);

    /* If there's one hit, *or* the top hit has > 99% of the total hits, then we
     * rewrite it to monomorphic. */
    else if ((100 * outcome_hits[1].hits) / total_hits >= 99)
        rewrite_monomorphic(tc, g, ins, bytecode_offset, outcome_hits[0].outcome);

    /* Otherwise, it's polymoprhic or megamorphic. */
    else
        rewrite_polymorphic(tc, g, ins, bytecode_offset, outcome_hits, MVM_VECTOR_ELEMS(outcome_hits));

    MVM_VECTOR_DESTROY(outcome_hits);
}
