#include "moar.h"

/* Sees if it will be possible to inline the target code ref, given we could
 * already identify a spesh candidate. Returns NULL if no inlining is possible
 * or a graph ready to be merged if it will be possible. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMCode *target,
                                               MVMSpeshCandidate *cand) {
    MVMSpeshGraph *ig;
    MVMSpeshBB    *bb;

    /* Check bytecode size is below the inline limit. */
    if (target->body.sf->body.bytecode_size > MVM_SPESH_MAX_INLINE_SIZE)
        return NULL;

    /* For now, if it has handlers, refuse to inline it. */
    if (target->body.sf->body.num_handlers > 0)
        return NULL;

    /* Build graph from the already-specialized bytecode. */
    ig = MVM_spesh_graph_create_from_cand(tc, target->body.sf, cand);

    /* Traverse graph, looking for anything that might prevent inlining. */
    bb = ig->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            /* Instruction may be marked directly as not being inlinable, in
             * which case we're done. */
            if (ins->info->no_inline)
                goto not_inlinable;

            /* If we have lexical access, make sure it's within the frame. */
            if (ins->info->opcode == MVM_OP_getlex)
                if (ins->operands[1].lex.outers > 0)
                    goto not_inlinable;
            else if (ins->info->opcode == MVM_OP_bindlex)
                if (ins->operands[0].lex.outers > 0)
                    goto not_inlinable;

            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* If we found nothing we can't inline, inlining is fine. */
    return ig;

    /* If we can't find a way to inline, we end up here. */
  not_inlinable:
    MVM_spesh_graph_destroy(tc, ig);
    return NULL;
}

/* Merges the inlinee's spesh graph into the inliner. */
void merge_graph(MVMThreadContext *tc, MVMSpeshGraph *inliner, MVMSpeshGraph *inlinee) {
    MVMSpeshFacts **merged_facts;
    MVMint32        i;

    /* Renumber the locals, lexicals, and basic blocks of the inlinee. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            if (ins->info->opcode == MVM_SSA_PHI) {
                for (i = 0; i < ins->info->num_operands; i++)
                    ins->operands[i].reg.orig += inliner->num_locals;
            }
            else {
                for (i = 0; i < ins->info->num_operands; i++)
                    switch (ins->info->operands[i] & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        ins->operands[i].reg.orig += inliner->num_locals;
                        break;
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex:
                        ins->operands[i].lex.idx += inliner->num_lexicals;
                        break;
                    default:
                        if (ins->info->operands[i] & MVM_operand_spesh_slot)
                            ins->operands[i].lit_i16 += inliner->num_spesh_slots;
                        break;
                    }
            }
            ins = ins->next;
        }
        bb->idx += inliner->num_bbs - 1; /* -1 as we won't include entry */
        bb = bb->linear_next;
    }

    /* Incorporate the basic blocks by concatening them onto the end of the
     * linear_next chain of the inliner; skip the inlinee's fake entry BB. */
    bb = inliner->entry;
    while (bb) {
        if (!bb->linear_next) {
            /* Found the end; insert and we're done. */
            bb->linear_next = inlinee->entry->linear_next;
            /* XXX Hack until we properly incorporate things... */
            bb->num_succ = 1;
            bb->succ = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshBB *));
            bb->succ[0] = bb->linear_next;
            /* XXX End hack. */
            bb = NULL;
        }
        else {
            bb = bb->linear_next;
        }
    }

    /* Merge facts. */
    merged_facts = MVM_spesh_alloc(tc, inliner,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMSpeshFacts *));
    memcpy(merged_facts, inliner->facts,
        inliner->num_locals * sizeof(MVMSpeshFacts *));
    memcpy(merged_facts + inliner->num_locals, inlinee->facts,
        inlinee->num_locals * sizeof(MVMSpeshFacts *));
    inliner->facts = merged_facts;

    /* Copy over spesh slots. */
    for (i = 0; i < inlinee->num_spesh_slots; i++)
        MVM_spesh_add_spesh_slot(tc, inliner, inlinee->spesh_slots[i]);

    /* Update total locals, lexicals, and basic blocks of the inliner. */
    inliner->num_bbs      += inlinee->num_bbs - 1;
    inliner->num_locals   += inlinee->num_locals;
    inliner->num_lexicals += inlinee->num_lexicals;
}

/* Drives the overall inlining process. */
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                      MVMSpeshIns *invoke, MVMSpeshGraph *inlinee) {
    /* Merge inlinee's graph into the inliner. */
    merge_graph(tc, inliner, inlinee);

    /* Re-write the argument passing instructions to poke values into the
     * appropriate slots. */

    /* Re-write invoke and returns to gotos. */

}
