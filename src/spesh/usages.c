#include "moar.h"

/* Adds a usage of an SSA value. */
void MVM_spesh_usages_add(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by) {
    MVMSpeshUseChainEntry *entry = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshUseChainEntry));
    entry->user = by;
    entry->next = facts->usage.users;
    facts->usage.users = entry;
}
void MVM_spesh_usages_add_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by) {
    MVM_spesh_usages_add(tc, g, MVM_spesh_get_facts(tc, g, used), by);
}

/* Removes a usage of an SSA value. */
void MVM_spesh_usages_delete(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts, MVMSpeshIns *by) {
    MVMSpeshUseChainEntry *cur = facts->usage.users;
    MVMSpeshUseChainEntry *prev = NULL;
    while (cur) {
        if (cur->user == by) {
            if (prev)
                prev->next = cur->next;
            else
                facts->usage.users = cur->next;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
    MVM_oops(tc, "Spesh: instruction %s missing from define-use chain", by->info->name);
}
void MVM_spesh_usages_delete_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used, MVMSpeshIns *by) {
    MVM_spesh_usages_delete(tc, g, MVM_spesh_get_facts(tc, g, used), by);
}

/* Marks that an SSA value is required for exception handling purposes. */
void MVM_spesh_usages_add_for_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshFacts *facts) {
    facts->usage.handler_required = 1;
}
void MVM_spesh_usages_add_for_handler_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand used) {
    MVM_spesh_usages_add_for_handler(tc, g, MVM_spesh_get_facts(tc, g, used));
}

/* Takes a spesh graph and adds usage information. */
static void add_usage_for_bb(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMint32 i;
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        /* Look through operands for reads and writes. */
        MVMint32 is_phi = ins->info->opcode == MVM_SSA_PHI;
        for (i = 0; i < ins->info->num_operands; i++) {
            /* Reads need usage tracking. */
            if ((is_phi && i > 0)
                || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)) {
                MVMSpeshFacts *facts = &(g->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i]);
                MVM_spesh_usages_add(tc, g, facts, ins);
            }

            /* Writes need the writing instruction to be specified. */
            if ((is_phi && i == 0)
                || (!is_phi && (ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg)) {
                MVMSpeshFacts *facts = &(g->facts[ins->operands[i].reg.orig][ins->operands[i].reg.i]);
                facts->writer    = ins;
            }
        }

        /* These all read as well as write a value, so bump usages. */
        switch (ins->info->opcode) {
            case MVM_OP_inc_i:
            case MVM_OP_inc_u:
            case MVM_OP_dec_i:
            case MVM_OP_dec_u: {
                MVMSpeshOperand reader;
                reader.reg.orig = ins->operands[0].reg.orig;
                reader.reg.i = ins->operands[0].reg.i - 1;
                MVM_spesh_usages_add_by_reg(tc, g, reader, ins);
                break;
            }
        }

        ins = ins->next;
    }

    /* Visit children. */
    for (i = 0; i < bb->num_children; i++)
        add_usage_for_bb(tc, g, bb->children[i]);
}
void MVM_spesh_usages_create_usage(MVMThreadContext *tc, MVMSpeshGraph *g) {
    add_usage_for_bb(tc, g, g->entry);
}

/* Takes a spesh graph with DU chains already build and correct, and builds up
 * deopt use chains for it. The algorithm keeps track of writers with reads
 * that have not yet been observed, and when we reach a deopt point adds the
 * deopt point to all such instructions. Loops result in encountering a PHI
 * node doing a read which we've not yet seen the write instruction for. In
 * this case, we keep them back until such a time we have processed all of the
 * preds of the basic block(s) that did those reads. */
typedef struct ActiveWrite ActiveWrite;
struct ActiveWrite {
    MVMSpeshFacts *writer;
    ActiveWrite *next;
};
typedef struct PendingRead PendingRead;
struct PendingRead {
    MVMSpeshBB *reading_bb;
    MVMSpeshIns *reader;
    MVMSpeshOperand operand;
    PendingRead *next;
};
typedef struct {
    /* Writes that have had all their reads encountered yet. */
    ActiveWrite *active_writes;

    /* Basic blocks that we have processed. */
    MVMuint32 *bbs_seen;

    /* Basic blocks that have had all the preds processed. */
    MVMuint32 *bbs_preds_seen;

    /* Reads whose processing is pending on a pred being seen. */
    PendingRead *pending_reads;
} DeoptAnalysisState;
static void mark_read_done(MVMThreadContext *tc, MVMSpeshIns *ins, MVMSpeshFacts *facts) {
    MVMSpeshUseChainEntry *use_entry = facts->usage.users;
    MVMuint32 found = 0;
    while (use_entry) {
        if (!use_entry->deopt_read_processed && use_entry->user == ins) {
            use_entry->deopt_read_processed = 1;
            found = 1;
            break;
        }
        use_entry = use_entry->next;
    }
    if (!found)
        MVM_oops(tc, "Spesh deopt analysis: read by %s missing", ins->info->name);
}
static void process_read(MVMThreadContext *tc, DeoptAnalysisState *state, MVMSpeshGraph *g,
                         MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshOperand operand) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, operand);
    if (facts->usage.deopt_write_processed) {
        /* Writer already processed. Just mark the read as consumed. */
        mark_read_done(tc, ins, facts);
    }
    else {
        /* Add as a pending read. */
        PendingRead *pr = MVM_spesh_alloc(tc, g, sizeof(PendingRead));
        pr->reading_bb = bb;
        pr->reader = ins;
        pr->operand = operand;
        pr->next = state->pending_reads;
        state->pending_reads = pr;
    }
}
static void process_write(MVMThreadContext *tc, DeoptAnalysisState *state, MVMSpeshGraph *g,
                          MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshOperand operand) {
    /* Mark the write as having been seen. */
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, operand);
    facts->usage.deopt_write_processed = 1;

    /* Provided it has usages, place it into the chain of writes with
     * outstanding reads. */
    if (facts->usage.users) {
        ActiveWrite *write = MVM_spesh_alloc(tc, g, sizeof(ActiveWrite));
        write->writer = facts;
        write->next = state->active_writes;
        state->active_writes = write;
    }
}
static void process_deopt(MVMThreadContext *tc, DeoptAnalysisState *state, MVMSpeshGraph *g,
                          MVMSpeshBB *bb, MVMSpeshIns *ins, MVMint32 deopt_idx) {
    /* Go through the active writers. Any that are no longer active will be
     * filtered out along the way as a side-effect of processing the deopt.
     * For each one that is still really active, add the deopt usage. */
    ActiveWrite *write = state->active_writes;
    ActiveWrite *prev_write = NULL;
    while (write) {
        MVMSpeshUseChainEntry *use_entry = write->writer->usage.users;
        MVMuint32 has_unread = 0;
        while (use_entry) {
            if (!use_entry->deopt_read_processed) {
                has_unread = 1;
                break;
            }
            use_entry = use_entry->next;
        }
        if (has_unread) {
            MVMSpeshDeoptUseEntry *deopt_entry = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshDeoptUseEntry));
            deopt_entry->deopt_idx = deopt_idx;
            deopt_entry->next = write->writer->usage.deopt_users;
            write->writer->usage.deopt_users = deopt_entry;
            prev_write = write;
        }
        else {
            /* Was fully read; drop from the list. */
            if (prev_write)
                prev_write->next = write->next;
            else
                state->active_writes = write->next;
        }
        write = write->next;
    }

    /* If this instruction has a write operand then it's in the deopt set too. */
    if (ins->info->num_operands >= 1 && (ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
        MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, ins->operands[0]);
        MVMSpeshDeoptUseEntry *deopt_entry = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshDeoptUseEntry));
        deopt_entry->deopt_idx = deopt_idx;
        deopt_entry->next = facts->usage.deopt_users;
        facts->usage.deopt_users = deopt_entry;
    }
}
static void process_bb_for_deopt_usage(MVMThreadContext *tc, DeoptAnalysisState *state,
                                       MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMuint32 i, have_newly_processed_preds;
    
    /* Walk the BB's instructions. */
    MVMSpeshIns *ins = bb->first_ins;
    while (ins) {
        MVMint16 opcode = ins->info->opcode;
        MVMuint8 is_phi = opcode == MVM_SSA_PHI;
        MVMSpeshAnn *ann;

        /* Process the read operands. */
        if (MVM_spesh_is_inc_dec_op(opcode)) {
            MVMSpeshOperand read = ins->operands[0];
            read.reg.i--;
            process_read(tc, state, g, bb, ins, read);
        }
        else if (is_phi) {
            for (i = 1; i < ins->info->num_operands; i++)
                process_read(tc, state, g, bb, ins, ins->operands[i]);
        }
        else {
            for (i = 0; i < ins->info->num_operands; i++)
                if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)
                    process_read(tc, state, g, bb, ins, ins->operands[i]);
        }

        /* If it's a deopt point, add currently unread writes as dependencies. */
        ann = ins->annotations;
        while (ann) {
            switch (ann->type) {
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    process_deopt(tc, state, g, bb, ins, ann->data.deopt_idx);
                    break;
            }
            ann = ann->next;
        }

        /* Process the write. */
        if (is_phi) {
            process_write(tc, state, g, bb, ins, ins->operands[0]);
        }
        else {
            for (i = 0; i < ins->info->num_operands; i++)
                if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg)
                    process_write(tc, state, g, bb, ins, ins->operands[i]);
        }

        ins = ins->next;
    }

    /* Mark this BB as seen, and then go through its successors and see if we
     * can mark any new preds as seen. */
    state->bbs_seen[bb->idx] = 1;
    have_newly_processed_preds = 0;
    for (i = 0; i < bb->num_succ; i++) {
        MVMSpeshBB *succ_bb = bb->succ[i];
        if (!state->bbs_preds_seen[succ_bb->idx]) {
            MVMuint32 all_preds_seen = 1;
            MVMuint32 j;
            for (j = 0; j < succ_bb->num_pred; j++) {
                if (!state->bbs_seen[succ_bb->pred[j]->idx]) {
                    all_preds_seen = 0;
                    break;
                }
            }
            if (all_preds_seen) {
                state->bbs_preds_seen[succ_bb->idx] = 1;
                have_newly_processed_preds = 1;
            }
        }
    }

    /* Process any pending reads - those seen prior to their write - that we may
     * now be able to process. */
    if (have_newly_processed_preds && state->pending_reads) {
        PendingRead *pr = state->pending_reads;
        PendingRead *prev_pr = NULL;
        while (pr) {
            /* See if we saw all of the preds of the reading basic block.
             * If so, we can mark the read as having taken place, and then
             * remove this entry from the pending read list. */
            if (state->bbs_preds_seen[pr->reading_bb->idx]) {
                mark_read_done(tc, pr->reader, MVM_spesh_get_facts(tc, g, pr->operand));
                if (prev_pr)
                    prev_pr->next = pr->next;
                else
                    state->pending_reads = pr->next;
            }
            else {
                prev_pr = pr;
            }

            pr = pr->next;
        }
    }

    /* Walk the children of this BB. */
    for (i = 0; i < bb->num_children; i++)
        process_bb_for_deopt_usage(tc, state, g, bb->children[i]);
}
void MVM_spesh_usages_create_deopt_usage(MVMThreadContext *tc, MVMSpeshGraph *g) {
    DeoptAnalysisState state;
    memset(&state, 0, sizeof(DeoptAnalysisState));
    state.bbs_seen = MVM_spesh_alloc(tc, g, sizeof(MVMuint32) * g->num_bbs);
    state.bbs_preds_seen = MVM_spesh_alloc(tc, g, sizeof(MVMuint32) * g->num_bbs);
    process_bb_for_deopt_usage(tc, &state, g, g->entry);
}

/* Adds an unconditional deopt usage (that is, not dependent on any particular
 * deopt point). */
void MVM_spesh_usages_add_unconditional_deopt_usage(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                    MVMSpeshFacts *facts) {
    MVMSpeshDeoptUseEntry *deopt_entry = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshDeoptUseEntry));
    deopt_entry->deopt_idx = -1;
    deopt_entry->next = facts->usage.deopt_users;
    facts->usage.deopt_users = deopt_entry;
}
void MVM_spesh_usages_add_unconditional_deopt_usage_by_reg(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                           MVMSpeshOperand operand) {
    MVM_spesh_usages_add_unconditional_deopt_usage(tc, g, MVM_spesh_get_facts(tc, g, operand));
}

/* Records that a certain deopt point must always be considered in use, even
 * if we don't see the annotation on a deopt instruction. This may be because
 * it serves as a "proxy" for all of the deopts that may take place inside of
 * an inlinee. */
void MVM_spesh_usages_retain_deopt_index(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint32 idx) {
    /* Just allocate it at the number of deopt addrs now; it'll only be
     * used for those in the immediate graph anyway. */
    if (!g->always_retained_deopt_idxs)
        g->always_retained_deopt_idxs = MVM_spesh_alloc(tc, g, g->num_deopt_addrs * sizeof(MVMuint32));
    g->always_retained_deopt_idxs[g->num_always_retained_deopt_idxs++] = idx;
}

/* Remove usages of deopt points that won't casue deopt. */
void MVM_spesh_usages_remove_unused_deopt(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMuint32 i, j;

    /* First, walk graph to find which deopt points are actually used. */
    MVMuint8 *deopt_used = MVM_spesh_alloc(tc, g, g->num_deopt_addrs);
    MVMSpeshBB *bb = g->entry;
    while (bb) {
        if (!bb->inlined) {
            MVMSpeshIns *ins = bb->first_ins;
            while (ins) {
                MVMSpeshAnn *ann = ins->annotations;
                while (ann) {
                    switch (ann->type) {
                        case MVM_SPESH_ANN_DEOPT_ONE_INS:
                        case MVM_SPESH_ANN_DEOPT_ALL_INS:
                        case MVM_SPESH_ANN_DEOPT_SYNTH:
                            if (ins->info->may_cause_deopt)
                                deopt_used[ann->data.deopt_idx] = 1;
                            break;
                    }
                    ann = ann->next;
                }
                ins = ins->next;
            }
        }
        bb = bb->linear_next;
    }

    /* Include those we must always retain. */
    for (i = 0; i < g->num_always_retained_deopt_idxs; i++)
        deopt_used[g->always_retained_deopt_idxs[i]] = 1;
    
    /* Now delete deopt usages that are of unused deopt indices. */
    for (i = 0; i < g->sf->body.num_locals; i++) {
        for (j = 0; j < g->fact_counts[i]; j++) {
            MVMSpeshFacts *facts = &(g->facts[i][j]);
            MVMSpeshDeoptUseEntry *du_entry = facts->usage.deopt_users;
            MVMSpeshDeoptUseEntry *prev_du_entry = NULL;
            while (du_entry) {
                if (du_entry->deopt_idx >= 0 && !deopt_used[du_entry->deopt_idx]) {
                    if (prev_du_entry)
                        prev_du_entry->next = du_entry->next;
                    else
                        facts->usage.deopt_users = du_entry->next;
                }
                else {
                    prev_du_entry = du_entry;
                }
                du_entry = du_entry->next;
            }
        }
    }
}

/* Checks if the value is used, either by another instruction in the graph or
 * by being needed for deopt. */
MVMuint32 MVM_spesh_usages_is_used(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return facts->usage.deopt_users || facts->usage.handler_required || facts->usage.users;
}

/* Checks if the value is used due to being required for deopt. */
MVMuint32 MVM_spesh_usages_is_used_by_deopt(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return facts->usage.deopt_users != NULL;
}

/* Checks if the value is used due to being required for exception handling. */
MVMuint32 MVM_spesh_usages_is_used_by_handler(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return facts->usage.handler_required;
}

/* Checks if there is precisely one known non-deopt user of the value. */
MVMuint32 MVM_spesh_usages_used_once(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, check);
    return !facts->usage.deopt_users && !facts->usage.handler_required &&
        facts->usage.users && !facts->usage.users->next;
}

/* Gets the count of usages, excluding use for deopt or handler purposes. */
MVMuint32 MVM_spesh_usages_count(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand check) {
    MVMuint32 count = 0;
    MVMSpeshUseChainEntry *cur = MVM_spesh_get_facts(tc, g, check)->usage.users;
    while (cur) {
        count++;
        cur = cur->next;
    }
    return count;
}

#if MVM_SPESH_CHECK_DU
/* Check the DU chains of a graph are well formed. */
void MVM_spesh_usages_check(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb;

    /* Clear the "did we see this in the graph" flags. */
    MVMuint32 i, j;
    for (i = 0; i < g->num_locals; i++) {
        for (j = 0; j < g->fact_counts[i]; j++) {
            MVMSpeshUseChainEntry *use_entry = g->facts[i][j].usage.users;
            while (use_entry) {
                use_entry->seen_in_graph = 0;
                use_entry = use_entry->next;
            }
            g->facts[i][j].usage.writer_seen_in_graph = 0;
        }
    }

    /* Now walk the instruction graph. */
    cur_bb = g->entry;
    while (cur_bb) {
        MVMSpeshIns *cur_ins = cur_bb->first_ins;
        while (cur_ins) {
            MVMint16 opcode = cur_ins->info->opcode;
            MVMuint8 is_phi = opcode == MVM_SSA_PHI;
            MVMuint8 is_inc_dec = MVM_spesh_is_inc_dec_op(opcode);
            MVMuint32 i;
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if ((is_phi && i > 0) || is_inc_dec ||
                        (!is_phi && (cur_ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg)) {
                    /* It's a read. */
                    MVMuint16 version = is_inc_dec ? cur_ins->operands[i].reg.i - 1 : cur_ins->operands[i].reg.i;
                    MVMSpeshFacts *facts = &(g->facts[cur_ins->operands[i].reg.orig][version]);
                    MVMSpeshUseChainEntry *use_entry = facts->usage.users;
                    MVMuint32 found = 0;
                    while (use_entry) {
                        if (!use_entry->seen_in_graph && use_entry->user == cur_ins) {
                            use_entry->seen_in_graph = 1;
                            found = 1;
                            break;
                        }
                        use_entry = use_entry->next;
                    }
                    if (!found)
                        MVM_oops(tc, "Malformed DU chain: reader %s of %d(%d) in BB %d missing\n%s",
                            is_phi ? "PHI" : cur_ins->info->name,
                            cur_ins->operands[i].reg.orig, cur_ins->operands[i].reg.i,
                            cur_bb->idx,
                            MVM_spesh_dump(tc, g));
                }
                if ((is_phi && i == 0)
                       || (!is_phi && (cur_ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_write_reg)) {
                    /* It's a write. Check the writer is this instruction. */
                    MVMSpeshFacts *facts = &(g->facts[cur_ins->operands[i].reg.orig][cur_ins->operands[i].reg.i]);
                    if (facts->writer != cur_ins)
                        MVM_oops(tc, "Malformed DU chain: writer %s of %d(%d) in BB %d is incorrect\n%s",
                            is_phi ? "PHI" : cur_ins->info->name,
                            cur_ins->operands[i].reg.orig, cur_ins->operands[i].reg.i,
                            cur_bb->idx,
                            MVM_spesh_dump(tc, g));
                    facts->usage.writer_seen_in_graph = 1;
                }
            }
            cur_ins = cur_ins->next;
        }
        cur_bb = cur_bb->linear_next;
    }

    /* Check that the "did we see this in the graph" flags are all set, in
     * order to spot things that not used in the graph, but still have
     * usage marked. */
    for (i = 0; i < g->num_locals; i++) {
        for (j = 0; j < g->fact_counts[i]; j++) {
            MVMSpeshFacts *facts = &(g->facts[i][j]);
            MVMSpeshUseChainEntry *use_entry = facts->usage.users;
            if (use_entry && j > 0 && !facts->dead_writer && !facts->usage.writer_seen_in_graph) {
                MVMSpeshIns *writer = facts->writer;
                MVMuint8 is_phi = writer && writer->info->opcode == MVM_SSA_PHI;
                MVM_oops(tc, "Malformed DU chain: writer %s of %d(%d) not in graph\n%s",
                    is_phi ? "PHI" : (writer ? writer->info->name : "MISSING WRITER"), i, j,
                    MVM_spesh_dump(tc, g));
            }
            while (use_entry) {
                if (!use_entry->seen_in_graph) {
                    MVMuint8 is_phi = use_entry->user->info->opcode == MVM_SSA_PHI;
                    MVM_oops(tc, "Malformed DU chain: reading %s of %d(%d) not in graph\n%s",
                        is_phi ? "PHI" : use_entry->user->info->name, i, j,
                        MVM_spesh_dump(tc, g));
                }
                use_entry = use_entry->next;
            }
        }
    }
}
#endif
