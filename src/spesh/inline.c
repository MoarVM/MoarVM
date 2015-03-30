#include "moar.h"

/* Ensures that a given compilation unit has access to the specified extop. */
static void demand_extop(MVMThreadContext *tc, MVMCompUnit *target_cu, MVMCompUnit *source_cu,
                         const MVMOpInfo *info) {
    MVMExtOpRecord *extops;
    MVMuint16 i, num_extops;

    MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)target_cu->body.update_mutex);

    /* See if the target compunit already has the extop. */
    extops     = target_cu->body.extops;
    num_extops = target_cu->body.num_extops;
    for (i = 0; i < num_extops; i++)
        if (extops[i].info == info) {
            MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)target_cu->body.update_mutex);
            return;
        }

    /* If not, need to add it. Locate it in the source CU. */
    extops     = source_cu->body.extops;
    num_extops = source_cu->body.num_extops;
    for (i = 0; i < num_extops; i++) {
        if (extops[i].info == info) {
            MVMuint32 size = (target_cu->body.num_extops + 1) * sizeof(MVMExtOpRecord);
            target_cu->body.extops = target_cu->body.extops
                ? MVM_realloc(target_cu->body.extops, size)
                : MVM_malloc(size);
            memcpy(&target_cu->body.extops[target_cu->body.num_extops],
                &extops[i], sizeof(MVMExtOpRecord));
            target_cu->body.num_extops++;
            MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)target_cu->body.update_mutex);
            return;
        }
    }

    /* Didn't find it; should be impossible. */
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)target_cu->body.update_mutex);
    MVM_exception_throw_adhoc(tc, "Spesh: inline failed to find source CU extop entry");
}

/* Sees if it will be possible to inline the target code ref, given we could
 * already identify a spesh candidate. Returns NULL if no inlining is possible
 * or a graph ready to be merged if it will be possible. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                                               MVMCode *target, MVMSpeshCandidate *cand) {
    MVMSpeshGraph *ig;
    MVMSpeshBB    *bb;

    /* Check inlining is enabled. */
    if (!tc->instance->spesh_inline_enabled)
        return NULL;

    /* Check bytecode size is within the inline limit. */
    if (target->body.sf->body.bytecode_size > MVM_SPESH_MAX_INLINE_SIZE)
        return NULL;

    /* Ensure that this isn't a recursive inlining. */
    if (target->body.sf == inliner->sf)
        return NULL;

    /* Ensure the candidate isn't still logging. */
    if (cand->sg)
        return NULL;

    /* Build graph from the already-specialized bytecode. */
    ig = MVM_spesh_graph_create_from_cand(tc, target->body.sf, cand, 0);

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
                if ((is_phi && i > 0)
                    || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg))
                    ig->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i].usages++;
            if (opcode == MVM_OP_inc_i || opcode == MVM_OP_inc_u ||
                    opcode == MVM_OP_dec_i || opcode == MVM_OP_dec_u)
                ig->facts[ins->operands[0].reg.orig][ins->operands[0].reg.i - 1].usages++;

            /* Instruction may be marked directly as not being inlinable, in
             * which case we're done. */
            if (!is_phi && ins->info->no_inline)
                goto not_inlinable;

            /* If we have lexical access, make sure it's within the frame. */
            if (ins->info->opcode == MVM_OP_getlex) {
                if (ins->operands[1].lex.outers > 0)
                    goto not_inlinable;
            }
            else if (ins->info->opcode == MVM_OP_bindlex) {
                if (ins->operands[0].lex.outers > 0)
                    goto not_inlinable;
            }

            /* Check we don't have too many args for inlining to work out. */
            if (ins->info->opcode == MVM_OP_sp_getarg_o ||
                    ins->info->opcode == MVM_OP_sp_getarg_i ||
                    ins->info->opcode == MVM_OP_sp_getarg_n ||
                    ins->info->opcode == MVM_OP_sp_getarg_s) {
                if (ins->operands[1].lit_i16 >= MAX_ARGS_FOR_OPT)
                    goto not_inlinable;
            }

            /* Ext-ops need special care in inter-comp-unit inlines. */
            if (ins->info->opcode == (MVMuint16)-1) {
                MVMCompUnit *target_cu = inliner->sf->body.cu;
                MVMCompUnit *source_cu = target->body.sf->body.cu;
                if (source_cu != target_cu)
                    demand_extop(tc, target_cu, source_cu, ins->info);
            }

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

/* Finds the deopt index of the return. */
static MVMint32 return_deopt_idx(MVMThreadContext *tc, MVMSpeshIns *invoke_ins) {
    MVMSpeshAnn *ann = invoke_ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS)
            return ann->data.deopt_idx;
        ann = ann->next;
    }
    MVM_exception_throw_adhoc(tc, "Spesh inline: return_deopt_idx failed");
}

/* The following routines fix references to per-compilation-unit things
 * that would be broken by inlining. */
static void fix_callsite(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                         MVMSpeshGraph *inlinee, MVMSpeshOperand *to_fix) {
    to_fix->callsite_idx = MVM_cu_callsite_add(tc, inliner->sf->body.cu,
        inlinee->sf->body.cu->body.callsites[to_fix->callsite_idx]);
}
static void fix_coderef(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                        MVMSpeshGraph *inlinee, MVMSpeshOperand *to_fix) {
    MVM_exception_throw_adhoc(tc, "Spesh inline: fix_coderef NYI");
}
static void fix_str(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                    MVMSpeshGraph *inlinee, MVMSpeshOperand *to_fix) {
    to_fix->lit_str_idx = MVM_cu_string_add(tc, inliner->sf->body.cu,
        inlinee->sf->body.cu->body.strings[to_fix->lit_str_idx]);
}
static void fix_wval(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                     MVMSpeshGraph *inlinee, MVMSpeshIns *to_fix) {
    /* Resolve object, then just put it into a spesh slot. (Could do some
     * smarter things like trying to see if the SC is referenced by both
     * compilation units, too.) */
    MVMCompUnit *cu  = inlinee->sf->body.cu;
    MVMint16     dep = to_fix->operands[1].lit_i16;
    MVMint64     idx = to_fix->info->opcode == MVM_OP_wval
        ? to_fix->operands[2].lit_i16
        : to_fix->operands[2].lit_i64;
    if (dep >= 0 && dep < cu->body.num_scs) {
        MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, dep);
        if (sc) {
            MVMObject *obj = MVM_sc_get_object(tc, sc, idx);
            MVMint16   ss  = MVM_spesh_add_spesh_slot(tc, inliner, (MVMCollectable *)obj);
            to_fix->info   = MVM_op_get_op(MVM_OP_sp_getspeshslot);
            to_fix->operands[1].lit_i16 = ss;
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Spesh inline: SC not yet resolved; lookup failed");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: invalid SC index found");
    }
}

/* Merges the inlinee's spesh graph into the inliner. */
static void merge_graph(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                 MVMSpeshGraph *inlinee, MVMCode *inlinee_code,
                 MVMSpeshIns *invoke_ins) {
    MVMSpeshFacts **merged_facts;
    MVMuint16      *merged_fact_counts;
    MVMint32        i, total_inlines, orig_deopt_addrs;

    /* If the inliner and inlinee are from different compilation units, we
     * potentially have to fix up extra things. */
    MVMint32 same_comp_unit = inliner->sf->body.cu == inlinee->sf->body.cu;

    /* Renumber the locals, lexicals, and basic blocks of the inlinee; also
     * re-write any indexes in annotations that need it. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16    opcode = ins->info->opcode;
            MVMSpeshAnn *ann    = ins->annotations;
            while (ann) {
                switch (ann->type) {
                case MVM_SPESH_ANN_FH_START:
                case MVM_SPESH_ANN_FH_END:
                case MVM_SPESH_ANN_FH_GOTO:
                    ann->data.frame_handler_index += inliner->num_handlers;
                    break;
                case MVM_SPESH_ANN_DEOPT_INLINE:
                    ann->data.deopt_idx += inliner->num_deopt_addrs;
                    break;
                case MVM_SPESH_ANN_INLINE_START:
                case MVM_SPESH_ANN_INLINE_END:
                    ann->data.inline_idx += inliner->num_inlines;
                    break;
                }
                ann = ann->next;
            }

            if (opcode == MVM_SSA_PHI) {
                for (i = 0; i < ins->info->num_operands; i++)
                    ins->operands[i].reg.orig += inliner->num_locals;
            }
            else {
                for (i = 0; i < ins->info->num_operands; i++) {
                    MVMuint8 flags = ins->info->operands[i];
                    switch (flags & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        ins->operands[i].reg.orig += inliner->num_locals;
                        break;
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex:
                        ins->operands[i].lex.idx += inliner->num_lexicals;
                        break;
                    default: {
                        MVMuint32 type = flags & MVM_operand_type_mask;
                        if (type == MVM_operand_spesh_slot) {
                            ins->operands[i].lit_i16 += inliner->num_spesh_slots;
                        }
                        else if (type == MVM_operand_callsite) {
                            if (!same_comp_unit)
                                fix_callsite(tc, inliner, inlinee, &(ins->operands[i]));
                        }
                        else if (type == MVM_operand_coderef) {
                            if (!same_comp_unit)
                                fix_coderef(tc, inliner, inlinee, &(ins->operands[i]));
                        }
                        else if (type == MVM_operand_str) {
                            if (!same_comp_unit)
                                fix_str(tc, inliner, inlinee, &(ins->operands[i]));
                        }
                        break;
                        }
                    }
                }
            }

            ins = ins->next;
        }
        bb->idx += inliner->num_bbs - 1; /* -1 as we won't include entry */
        bb->inlined = 1;
        bb = bb->linear_next;
    }

    /* Incorporate the basic blocks by concatening them onto the end of the
     * linear_next chain of the inliner; skip the inlinee's fake entry BB. */
    bb = inliner->entry;
    while (bb) {
        if (!bb->linear_next) {
            /* Found the end; insert and we're done. */
            bb->linear_next = inlinee->entry->linear_next;
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

    /* If they are from separate compilation units, make another pass through
     * to fix up on wvals. Note we can't do this in the first pass as we must
     * not modify the spesh slots once we've got started with the rewrites.
     * Now we've resolved all that, we're good to map wvals elsewhere into
     * some extra spesh slots. */
    if (!same_comp_unit) {
        bb = inlinee->entry;
        while (bb) {
            MVMSpeshIns *ins = bb->first_ins;
            while (ins) {
                MVMuint16 opcode = ins->info->opcode;
                if (opcode == MVM_OP_wval || opcode == MVM_OP_wval_wide)
                    fix_wval(tc, inliner, inlinee, ins);
                ins = ins->next;
            }
            bb = bb->linear_next;
        }
    }

    /* Merge de-opt tables, if needed. */
    orig_deopt_addrs = inliner->num_deopt_addrs;
    if (inlinee->num_deopt_addrs) {
        assert(inlinee->deopt_addrs != inliner->deopt_addrs);
        inliner->alloc_deopt_addrs += inlinee->alloc_deopt_addrs;
        if (inliner->deopt_addrs)
            inliner->deopt_addrs = MVM_realloc(inliner->deopt_addrs,
                inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        else
            inliner->deopt_addrs = MVM_malloc(inliner->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        memcpy(inliner->deopt_addrs + inliner->num_deopt_addrs * 2,
            inlinee->deopt_addrs, inlinee->alloc_deopt_addrs * sizeof(MVMint32) * 2);
        inliner->num_deopt_addrs += inlinee->num_deopt_addrs;
    }

    /* Merge inlines table, and add us an entry too. */
    total_inlines = inliner->num_inlines + inlinee->num_inlines + 1;
    inliner->inlines = inliner->num_inlines
        ? MVM_realloc(inliner->inlines, total_inlines * sizeof(MVMSpeshInline))
        : MVM_malloc(total_inlines * sizeof(MVMSpeshInline));
    memcpy(inliner->inlines + inliner->num_inlines, inlinee->inlines,
        inlinee->num_inlines * sizeof(MVMSpeshInline));
    for (i = inliner->num_inlines; i < total_inlines - 1; i++) {
        inliner->inlines[i].locals_start += inliner->num_locals;
        inliner->inlines[i].lexicals_start += inliner->num_lexicals;
        inliner->inlines[i].return_deopt_idx += orig_deopt_addrs;
    }
    inliner->inlines[total_inlines - 1].code           = inlinee_code;
    inliner->inlines[total_inlines - 1].g              = inlinee;
    inliner->inlines[total_inlines - 1].locals_start   = inliner->num_locals;
    inliner->inlines[total_inlines - 1].lexicals_start = inliner->num_lexicals;
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_VOID;
        break;
    case MVM_OP_invoke_o:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_OBJ;
        break;
    case MVM_OP_invoke_i:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_INT;
        break;
    case MVM_OP_invoke_n:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_NUM;
        break;
    case MVM_OP_invoke_s:
        inliner->inlines[total_inlines - 1].res_reg = invoke_ins->operands[0].reg.orig;
        inliner->inlines[total_inlines - 1].res_type = MVM_RETURN_STR;
        break;
    default:
        MVM_exception_throw_adhoc(tc, "Spesh inline: unknown invoke instruction");
    }
    inliner->inlines[total_inlines - 1].return_deopt_idx = return_deopt_idx(tc, invoke_ins);
    inliner->num_inlines = total_inlines;

    /* Create/update per-specialization local and lexical type maps. */
    if (!inliner->local_types) {
        MVMint32 local_types_size = inliner->num_locals * sizeof(MVMuint16);
        inliner->local_types = MVM_malloc(local_types_size);
        memcpy(inliner->local_types, inliner->sf->body.local_types, local_types_size);
    }
    inliner->local_types = MVM_realloc(inliner->local_types,
        (inliner->num_locals + inlinee->num_locals) * sizeof(MVMuint16));
    memcpy(inliner->local_types + inliner->num_locals,
        inlinee->local_types ? inlinee->local_types : inlinee->sf->body.local_types,
        inlinee->num_locals * sizeof(MVMuint16));
    if (!inliner->lexical_types) {
        MVMint32 lexical_types_size = inliner->num_lexicals * sizeof(MVMuint16);
        inliner->lexical_types = MVM_malloc(lexical_types_size);
        memcpy(inliner->lexical_types, inliner->sf->body.lexical_types, lexical_types_size);
    }
    inliner->lexical_types = MVM_realloc(inliner->lexical_types,
        (inliner->num_lexicals + inlinee->num_lexicals) * sizeof(MVMuint16));
    memcpy(inliner->lexical_types + inliner->num_lexicals,
        inlinee->lexical_types ? inlinee->lexical_types : inlinee->sf->body.lexical_types,
        inlinee->num_lexicals * sizeof(MVMuint16));

    /* Merge handlers. */
    if (inlinee->num_handlers) {
        MVMuint32 total_handlers = inliner->num_handlers + inlinee->num_handlers;
        if (inliner->handlers == inliner->sf->body.handlers) {
            /* Original handlers table; need a copy. */
            MVMFrameHandler *new_handlers = MVM_malloc(total_handlers * sizeof(MVMFrameHandler));
            memcpy(new_handlers, inliner->handlers,
                inliner->num_handlers * sizeof(MVMFrameHandler));
            inliner->handlers = new_handlers;
        }
        else {
            /* Probably already did some inlines into this frame; resize. */
            inliner->handlers = MVM_realloc(inliner->handlers,
                total_handlers * sizeof(MVMFrameHandler));
        }
        memcpy(inliner->handlers + inliner->num_handlers, inlinee->handlers,
            inlinee->num_handlers * sizeof(MVMFrameHandler));
        for (i = inliner->num_handlers; i < total_handlers; i++) {
            inliner->handlers[i].block_reg += inliner->num_locals;
            inliner->handlers[i].label_reg += inliner->num_locals;
        }
    }

    /* Update total locals, lexicals, basic blocks, and handlers of the
     * inliner. */
    inliner->num_bbs      += inlinee->num_bbs - 1;
    inliner->num_locals   += inlinee->num_locals;
    inliner->num_lexicals += inlinee->num_lexicals;
    inliner->num_handlers += inlinee->num_handlers;
}

/* Tweak the successor of a BB, also updating the target BBs pred. */
static void tweak_succ(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshBB *new_succ) {
    if (bb->num_succ == 0) {
        bb->succ = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
        bb->num_succ = 1;
    }
    if (bb->num_succ == 1)
        bb->succ[0] = new_succ;
    else
        MVM_exception_throw_adhoc(tc, "Spesh inline: unexpected num_succ");
    if (new_succ->num_pred == 0) {
        new_succ->pred = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
        new_succ->num_pred = 1;
        new_succ->pred[0] = bb;
    }
    else {
        MVMint32 found = 0;
        MVMint32 i;
        for (i = 0; i < new_succ->num_pred; i++)
            if (new_succ->pred[i]->idx + 1 == new_succ->idx) {
                new_succ->pred[i] = bb;
                found = 1;
                break;
            }
        if (!found)
            MVM_exception_throw_adhoc(tc,
                "Spesh inline: could not find appropriate pred to update\n");
    }
}

/* Finds return instructions and re-writes them into gotos, doing any needed
 * boxing or unboxing. */
static void return_to_set(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *return_ins, MVMSpeshOperand target) {
    MVMSpeshOperand *operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    operands[0]               = target;
    operands[1]               = return_ins->operands[0];
    return_ins->info          = MVM_op_get_op(MVM_OP_set);
    return_ins->operands      = operands;
}

static void return_to_box(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *return_bb,
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

static void rewrite_int_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
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
static void rewrite_num_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
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

static void rewrite_str_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
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

static void rewrite_obj_return(MVMThreadContext *tc, MVMSpeshGraph *g,
                        MVMSpeshBB *return_bb, MVMSpeshIns *return_ins,
                        MVMSpeshBB *invoke_bb, MVMSpeshIns *invoke_ins) {
    switch (invoke_ins->info->opcode) {
    case MVM_OP_invoke_v:
        MVM_spesh_manipulate_delete_ins(tc, g, return_bb, return_ins);
        break;
    case MVM_OP_invoke_o:
        return_to_set(tc, g, return_ins, invoke_ins->operands[0]);
        break;
    default:
        MVM_exception_throw_adhoc(tc,
            "Spesh inline: unhandled case of return_o");
    }
}

static void rewrite_returns(MVMThreadContext *tc, MVMSpeshGraph *inliner,
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
                if (invoke_ins->info->opcode == MVM_OP_invoke_v) {
                    MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                        invoke_bb->succ[0]);
                    tweak_succ(tc, inliner, bb, invoke_bb->succ[0]);
                }
                else {
                    MVM_exception_throw_adhoc(tc,
                        "Spesh inline: return_v/invoke_[!v] mismatch");
                }
                break;
            case MVM_OP_return_i:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb->succ[0]);
                rewrite_int_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_n:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb->succ[0]);
                rewrite_num_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_s:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb->succ[0]);
                rewrite_str_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            case MVM_OP_return_o:
                MVM_spesh_manipulate_insert_goto(tc, inliner, bb, ins,
                    invoke_bb->succ[0]);
                tweak_succ(tc, inliner, bb, invoke_bb->succ[0]);
                rewrite_obj_return(tc, inliner, bb, ins, invoke_bb, invoke_ins);
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }
}

/* Re-writes argument passing and parameter taking instructions to simple
 * register set operations. */
static void rewrite_args(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                  MVMSpeshGraph *inlinee, MVMSpeshBB *invoke_bb,
                  MVMSpeshCallInfo *call_info) {
    /* Look for param-taking instructions. Track what arg instructions we
     * use in the process. */
    MVMSpeshBB *bb = inlinee->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            MVMuint16    opcode = ins->info->opcode;
            MVMSpeshIns *next   = ins->next;
            switch (opcode) {
            case MVM_OP_sp_getarg_o:
            case MVM_OP_sp_getarg_i:
            case MVM_OP_sp_getarg_n:
            case MVM_OP_sp_getarg_s: {
                MVMuint16    idx     = ins->operands[1].lit_i16;
                MVMSpeshIns *arg_ins = call_info->arg_ins[idx];
                switch (arg_ins->info->opcode) {
                case MVM_OP_arg_i:
                case MVM_OP_arg_n:
                case MVM_OP_arg_s:
                case MVM_OP_arg_o:
                    /* Arg passer just becomes a set instruction; delete the
                     * parameter-taking instruction. */
                    arg_ins->info        = MVM_op_get_op(MVM_OP_set);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_get_facts(tc, inliner, arg_ins->operands[0])->usages++;
                    break;
                case MVM_OP_argconst_i:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_i64);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_get_facts(tc, inliner, arg_ins->operands[0])->usages++;
                    break;
                case MVM_OP_argconst_n:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_n64);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_get_facts(tc, inliner, arg_ins->operands[0])->usages++;
                    break;
                case MVM_OP_argconst_s:
                    arg_ins->info        = MVM_op_get_op(MVM_OP_const_s);
                    arg_ins->operands[0] = ins->operands[0];
                    MVM_spesh_manipulate_delete_ins(tc, inliner, bb, ins);
                    MVM_spesh_get_facts(tc, inliner, arg_ins->operands[0])->usages++;
                    break;
                default:
                    MVM_exception_throw_adhoc(tc,
                        "Spesh inline: unhandled arg instruction %d",
                        arg_ins->info->opcode);
                }
                break;
            }
            }
            ins = next;
        }
        bb = bb->linear_next;
    }

    /* Delete the prepargs instruction. */
    MVM_spesh_manipulate_delete_ins(tc, inliner, invoke_bb, call_info->prepargs_ins);
}

/* Annotates first and last instruction in post-processed inlinee with start
 * and end inline annotations. */
static void annotate_inline_start_end(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                               MVMSpeshGraph *inlinee, MVMint32 idx) {
    /* Annotate first instruction. */
    MVMSpeshAnn *start_ann     = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
    MVMSpeshBB *bb             = inlinee->entry->succ[0];
    start_ann->next            = bb->first_ins->annotations;
    start_ann->type            = MVM_SPESH_ANN_INLINE_START;
    start_ann->data.inline_idx = idx;
    bb->first_ins->annotations = start_ann;

    /* Now look for last instruction and annotate it. */
    while (bb) {
        if (!bb->linear_next) {
            MVMSpeshAnn *end_ann      = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshAnn));
            end_ann->next             = bb->last_ins->annotations;
            end_ann->type             = MVM_SPESH_ANN_INLINE_END;
            end_ann->data.inline_idx  = idx;
            bb->last_ins->annotations = end_ann;
        }
        bb = bb->linear_next;
    }
}

/* Drives the overall inlining process. */
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
                      MVMSpeshCallInfo *call_info, MVMSpeshBB *invoke_bb,
                      MVMSpeshIns *invoke_ins, MVMSpeshGraph *inlinee,
                      MVMCode *inlinee_code) {
    /* Merge inlinee's graph into the inliner. */
    merge_graph(tc, inliner, inlinee, inlinee_code, invoke_ins);

    /* If we're profiling, note it's an inline. */
    if (inlinee->entry->linear_next->first_ins->info->opcode == MVM_OP_prof_enterspesh) {
        MVMSpeshIns *profenter         = inlinee->entry->linear_next->first_ins;
        profenter->info                = MVM_op_get_op(MVM_OP_prof_enterinline);
        profenter->operands            = MVM_spesh_alloc(tc, inliner, sizeof(MVMSpeshOperand));
        profenter->operands[0].lit_i16 = MVM_spesh_add_spesh_slot(tc, inliner,
            (MVMCollectable *)inlinee->sf);
    }

    /* Re-write returns to a set and goto. */
    rewrite_returns(tc, inliner, inlinee, invoke_bb, invoke_ins);

    /* Re-write the argument passing instructions to poke values into the
     * appropriate slots. */
    rewrite_args(tc, inliner, inlinee, invoke_bb, call_info);

    /* Annotate first and last instruction with inline table annotations. */
    annotate_inline_start_end(tc, inliner, inlinee, inliner->num_inlines - 1);

    /* Finally, turn the invoke instruction into a goto. */
    invoke_ins->info = MVM_op_get_op(MVM_OP_goto);
    invoke_ins->operands[0].ins_bb = inlinee->entry->linear_next;
    tweak_succ(tc, inliner, invoke_bb, inlinee->entry->linear_next);
}
