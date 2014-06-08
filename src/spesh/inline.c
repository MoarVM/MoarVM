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

    /* Traverse graph, looking for anything that might prevent inlining and
     * also building usage counts up. */
    bb = ig->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            /* Track usages. */
            MVMint32 opcode = ins->info->opcode;
            MVMint32 is_phi = opcode == MVM_SSA_PHI;
            MVMuint8 i;
            for (i = 0; i < ins->info->num_operands; i++)
                if (is_phi && i > 0 || !is_phi &&
                    (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)
                    ig->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i].usages++;
            if (opcode == MVM_OP_inc_i || opcode == MVM_OP_inc_u ||
                    opcode == MVM_OP_dec_i || opcode == MVM_OP_dec_u)
                ig->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i - 1].usages++;

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
    MVMuint16      *merged_fact_counts;
    MVMint32        i;

    /* Renumber the locals, lexicals, and basic blocks of the inlinee; also
     * re-write any indexes in annotations that need it. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMSpeshAnn *ann = ins->annotations;
            while (ann) {
                switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    ann->data.deopt_idx += inliner->num_deopt_addrs;
                    break;
                }
                ann = ann->next;
            }

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
    merged_fact_counts = MVM_spesh_alloc(tc, inliner,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMuint16));
    memcpy(merged_fact_counts, inliner->fact_counts,
        inliner->num_locals * sizeof(MVMuint16));
    memcpy(merged_fact_counts + inliner->num_locals, inlinee->fact_counts,
        inlinee->num_locals * sizeof(MVMuint16));
    inliner->fact_counts = merged_fact_counts;

    /* Copy over spesh slots. */
    for (i = 0; i < inlinee->num_spesh_slots; i++)
        MVM_spesh_add_spesh_slot(tc, inliner, inlinee->spesh_slots[i]);

    /* Merge de-opt tables, if needed. */
    if (inlinee->num_deopt_addrs) {
        inliner->alloc_deopt_addrs += inlinee->alloc_deopt_addrs;
        if (inliner->deopt_addrs)
            inliner->deopt_addrs = realloc(inliner->deopt_addrs,
                inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        else
            inliner->deopt_addrs = malloc(inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        memcpy(inliner->deopt_addrs + inliner->num_deopt_addrs * 2,
            inlinee->deopt_addrs, inlinee->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        inliner->num_deopt_addrs += inlinee->num_deopt_addrs;
    }

    /* Update total locals, lexicals, and basic blocks of the inliner. */
    inliner->num_bbs      += inlinee->num_bbs - 1;
    inliner->num_locals   += inlinee->num_locals;
    inliner->num_lexicals += inlinee->num_lexicals;
}

/* Finds return instructions and re-writes them into gotos, doing any needed
 * boxing or unboxing. */
void return_to_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *return_ins, MVMSpeshOperand target) {
    MVMSpeshOperand *operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    operands[0]               = target;
    operands[1]               = return_ins->operands[0];
    return_ins->info          = MVM_op_get_op(MVM_OP_set);
    return_ins->operands      = operands;
}
void return_to_box(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *return_bb,
                   MVMSpeshIns *return_ins, MVMSpeshOperand target,
                   MVMuint16 box_type_op, MVMuint16 box_op) {
    /* Create and insert boxing instruction after current return instruction. */
    MVMSpeshIns      *box_ins     = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    MVMSpeshOperand *box_operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    box_ins->info                 = MVM_op_get_op(box_op);
    box_ins->operands             = box_operands;
    box_operands[0]               = target;
    box_operands[1]               = return_ins->operands[0];
    box_operands[2]               = target;
    MVM_spesh_manipulate_insert_ins(tc, return_bb, return_ins, box_ins);

    /* Now turn return instruction node into lookup of appropraite box
     * type. */
    return_ins->info        = MVM_op_get_op(box_type_op);
    return_ins->operands[0] = target;
}
void rewrite_int_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, return_bb, return_ins);
        break;
    case MVM_OP_invoke_i:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_i, MVM_OP_box_i);
        break;
    default:
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: unhandled case of return_i");
    }
}
void rewrite_num_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, return_bb, return_ins);
        break;
    case MVM_OP_invoke_n:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_n, MVM_OP_box_n);
        break;
    default:
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: unhandled case of return_n");
    }
}
void rewrite_str_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, return_bb, return_ins);
        break;
    case MVM_OP_invoke_s:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    case MVM_OP_invoke_o:
        return_to_box(tc, g, return_bb, return_ins, invoke_ins->operands[0],
            MVM_OP_hllboxtype_s, MVM_OP_box_s);
        break;
    default:
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: unhandled case of return_s");
    }
}
void rewrite_obj_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, return_bb, return_ins);
        break;
    case MVM_OP_invoke_o:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    default:
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: unhandled case of return_o");
    }
}
void rewrite_returns(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                     MVMSpeshGraph *inlinee, MVMSpeshBB *invoke_bb,
                     MVMSpeshIns *invoke_ins) {
    /* Locate return instructions. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16 opcode = ins->info->opcode;
            switch (opcode) {
            case MVM_OP_return:
                if (invoke_ins->info->opcode == MVM_OP_invoke_v)
                    MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                        invoke_bb->succ[0]);
                else
                    MVM_exception_throw_adhoc(tc,
                        "Spesh inline: return_v/invoke_[!v] mismatch");
                break;
            case MVM_OP_return_i:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                rewrite_int_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_n:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                rewrite_num_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_s:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                rewrite_str_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_o:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                rewrite_obj_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }
}

/* Drives the overall inlining process. */
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                      MVMSpeshCallInfo *call_info, MVMSpeshBB *invoke_bb,
                      MVMSpeshIns *invoke_ins, MVMSpeshGraph *inlinee) {
    /* Merge inlinee's graph into the inliner. */
    merge_graph(tc, inliner, inlinee);

    /* Re-write returns to a set and goto. */
    rewrite_returns(tc, inliner, inlinee, invoke_bb, invoke_ins);

    /* Re-write the argument passing instructions to poke values into the
     * appropriate slots. */
}
