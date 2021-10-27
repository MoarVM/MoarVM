#include "moar.h"

/* This file contains various routines for manipulating a spesh graph, such
 * as adding/removing/replacing instructions. */

/* Deletes an instruction, and does any fact changes as a result. */
void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshGraph *g,
                                     MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshIns *prev, *next;

    /* If the instruction is in an already dead basic block, nothing to do. */
    if (bb->dead)
        return;

    /* Remove it from the double linked list. */
    prev = ins->prev;
    next = ins->next;
    if (prev)
        prev->next = next;
    else
        bb->first_ins = next;
    if (next)
        next->prev = prev;
    else
        bb->last_ins = prev;

    /* Move its annotations. */
    while (ins->annotations) {
        MVMSpeshAnn *ann      = ins->annotations;
        MVMSpeshAnn *ann_next = ann->next;
        /* Special case: we make fake entires into the handler table for
         * a handler that covers the whole inline, so we can proprerly
         * process inline boundaries. Those need to move line an inline
         * end annotation. */
        int tweaked_type = ann->type == MVM_SPESH_ANN_FH_END &&
                g->handlers[ann->data.frame_handler_index].category_mask == MVM_EX_INLINE_BOUNDARY
            ? MVM_SPESH_ANN_INLINE_END
            : ann->type;
        switch (tweaked_type) {
            case MVM_SPESH_ANN_FH_START:
            case MVM_SPESH_ANN_FH_GOTO:
            case MVM_SPESH_ANN_INLINE_START:
            case MVM_SPESH_ANN_INLINE_END:
            case MVM_SPESH_ANN_DEOPT_OSR:
                /* These move to the next instruction. */
                if (!next) {
                    MVMSpeshBB *dest_bb = bb->linear_next;
                    while (dest_bb && !dest_bb->first_ins)
                        dest_bb = dest_bb->linear_next;
                    if (dest_bb)
                        next = dest_bb->first_ins;
                }
                if (next) {
                    ann->next = next->annotations;
                    next->annotations = ann;
                }
                break;
            case MVM_SPESH_ANN_FH_END:
                /* This moves to the previous instruction. */
                if (!prev) {
                    MVMSpeshBB *prev_bb = MVM_spesh_graph_linear_prev(tc, g, bb);
                    while (prev_bb && !prev_bb->last_ins)
                        prev_bb = MVM_spesh_graph_linear_prev(tc, g, prev_bb);
                    if (prev_bb)
                        prev = prev_bb->last_ins;
                }
                if (prev) {
                    ann->next = prev->annotations;
                    prev->annotations = ann;
                }
                break;
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
                /* This moves to the previous instruction, but we need to put
                 * it on the end of the list, so the earlier deopt point will
                 * win when searching for deopt points. Otherwise, we can
                 * deopt to a later place than we should have. Also, we should
                 * never move a later deopt instruction onto something with a
                 * deopt all or an inline, otherwise it can confuse uninlining. */
                if (!prev) {
                    MVMSpeshBB *prev_bb = MVM_spesh_graph_linear_prev(tc, g, bb);
                    while (prev_bb && !prev_bb->last_ins)
                        prev_bb = MVM_spesh_graph_linear_prev(tc, g, prev_bb);
                    if (prev_bb)
                        prev = prev_bb->last_ins;
                }
                if (prev) {
                    MVMSpeshAnn *append_to = prev->annotations;
                    MVMint32 conflict = 0;
                    while (append_to) {
                        if (append_to->type == MVM_SPESH_ANN_DEOPT_ALL_INS ||
                                append_to->type == MVM_SPESH_ANN_DEOPT_INLINE) {
                            conflict = 1;
                            break;
                        }
                        if (!append_to->next)
                            break;
                        append_to = append_to->next;
                    }
                    if (!conflict) {
                        if (append_to)
                            append_to->next = ann;
                        else
                            prev->annotations = ann;
                        ann->next = NULL;
                    }
                }
                break;
        }
        ins->annotations = ann_next;
    }

    MVM_spesh_manipulate_cleanup_ins_deps(tc, g, ins);
}

/* When deleting an instruction, we can mark any writes of the instruction as
 * dead, and also decrement the usage counts on anything that is read. This is
 * called by MVM_spesh_manipulate_delete_ins, but provided separately for when
 * an instruction goes away by virtue of a whole basic block dying. */ 
void MVM_spesh_manipulate_cleanup_ins_deps(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshIns *ins) {
    MVMint16 opcode = ins->info->opcode;
    if (opcode == MVM_SSA_PHI) {
        MVMint32 i;
        MVM_spesh_get_facts(tc, g, ins->operands[0])->dead_writer = 1;
        for (i = 1; i < ins->info->num_operands; i++)
            MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[i], ins);
    }
    else {
        MVMint32 i;
        MVMuint8 is_inc_dec = MVM_spesh_is_inc_dec_op(opcode);
        for (i = 0; i < ins->info->num_operands; i++) {
            MVMint32 rw = ins->info->operands[i] & MVM_operand_rw_mask;
            if (rw == MVM_operand_write_reg)
                MVM_spesh_get_facts(tc, g, ins->operands[i])->dead_writer = 1;
            else if (rw == MVM_operand_read_reg)
                MVM_spesh_usages_delete_by_reg(tc, g, ins->operands[i], ins);
            if (is_inc_dec) {
                MVMSpeshOperand read = ins->operands[i];
                read.reg.i--;
                MVM_spesh_usages_delete_by_reg(tc, g, read, ins);
            }
        }
    }
}

/* Inserts an instruction after the specified instruction, or at the start of
 * the basic block if the instruction is NULL. */
void MVM_spesh_manipulate_insert_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *previous, MVMSpeshIns *to_insert) {
    /* Do the insertion. */
    MVMSpeshIns *next;
    if (previous) {
        next = previous->next;
        previous->next = to_insert;
    } else {
        next = bb->first_ins;
        bb->first_ins = to_insert;
    }
    to_insert->next = next;
    if (next) {
        next->prev = to_insert;
    } else {
        bb->last_ins = to_insert;
    }
    to_insert->prev = previous;

    /* If the instruction after the inserted one has an OSR deopt annotation,
     * we move it onto the instruction we just inserted. */
    if (next && next->annotations) {
        MVMSpeshAnn *ann = next->annotations;
        MVMSpeshAnn *prev_ann = NULL;
        while (ann) {
            if (ann->type == MVM_SPESH_ANN_DEOPT_OSR) {
                if (prev_ann)
                    prev_ann->next = ann->next;
                else
                    next->annotations = ann->next;
                ann->next = to_insert->annotations;
                to_insert->annotations = ann;
                break;
            }
            prev_ann = ann;
            ann = ann->next;
        }
    }
}

/* Inserts a goto. */
void MVM_spesh_manipulate_insert_goto(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins, MVMSpeshBB *target) {
    MVMSpeshIns *inserted_goto = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
    MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ));
    inserted_goto->info        = MVM_op_get_op(MVM_OP_goto);
    inserted_goto->operands    = operands;
    operands[0].ins_bb         = target;
    MVM_spesh_manipulate_insert_ins(tc, bb, ins, inserted_goto);
}

/* Adds a successor to a basic block, also adding to the list of
 * predecessors of the added successor. */
void MVM_spesh_manipulate_add_successor(MVMThreadContext *tc, MVMSpeshGraph *g,
                                        MVMSpeshBB *bb, MVMSpeshBB *succ) {
    MVMSpeshBB **new_succ, **new_pred;

    /* Add to successors. */
    new_succ = MVM_spesh_alloc(tc, g, (bb->num_succ + 1) * sizeof(MVMSpeshBB *));
    if (bb->num_succ)
        memcpy(new_succ, bb->succ, bb->num_succ * sizeof(MVMSpeshBB *));
    new_succ[bb->num_succ] = succ;
    bb->succ = new_succ;
    bb->num_succ++;

    /* And to successor's predecessors. */
    new_pred = MVM_spesh_alloc(tc, g, (succ->num_pred + 1) * sizeof(MVMSpeshBB *));
    if (succ->num_pred)
        memcpy(new_pred, succ->pred, succ->num_pred * sizeof(MVMSpeshBB *));
    new_pred[succ->num_pred] = bb;
    succ->pred = new_pred;
    succ->num_pred++;
}

/* Removes a successor to a basic block, also removing it from the list of
 * predecessors. */
void MVM_spesh_manipulate_remove_successor(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshBB *succ) {
    MVMSpeshBB ** const   bb_succ = bb->succ;
    MVMSpeshBB ** const succ_pred = succ->pred;
    const MVMuint16   bb_num_succ = --bb->num_succ;
    const MVMuint16 succ_num_pred = --succ->num_pred;
    MVMuint16 i, k;

    for (i = 0; i <= bb_num_succ; i++) {
        if (bb_succ[i] == succ) {
            break;
        }
    }

    if (bb_succ[i] != succ) {
        MVM_oops(tc, "Didn't find the successor to remove from a Spesh Basic Block");
    }

    /* Remove the succ from the list, shuffle other successors back in place. */
    for (k = i; k < bb_num_succ; k++) {
        bb_succ[k] = bb_succ[k + 1];
    }

    bb_succ[bb_num_succ] = NULL;

    /* Now hunt the bb in the succ's pred, so that we remove all traces of the connection. */
    for (i = 0; i <= succ_num_pred; i++) {
        if (succ_pred[i] == bb) {
            break;
        }
    }

    if (succ_pred[i] != bb) {
        MVM_oops(tc, "Didn't find the predecessor to remove from a Spesh Basic Block");
    }

    for (k = i; k < succ_num_pred; k++) {
        succ_pred[k] = succ_pred[k + 1];
    }

    succ_pred[succ_num_pred] = NULL;
}

/* Removes successors from a basic block that point to handlers.
   Useful for optimizations that turn throwish ops into non-throwing ones. */
void MVM_spesh_manipulate_remove_handler_successors(MVMThreadContext *tc, MVMSpeshBB *bb) {
    int i;
    for (i = 0; i < bb->num_handler_succ; i++) {
        MVM_spesh_manipulate_remove_successor(tc, bb, bb->handler_succ[i]);
        bb->handler_succ[i] = NULL;
    }
    bb->num_handler_succ = 0;
}

static void ensure_more_temps(MVMThreadContext *tc, MVMSpeshGraph *g) {
    if (g->num_temps == g->alloc_temps) {
        MVMSpeshTemporary *new_temps;
        g->alloc_temps += 4;
        new_temps = MVM_spesh_alloc(tc, g, g->alloc_temps * sizeof(MVMSpeshTemporary));
        if (g->num_temps)
            memcpy(new_temps, g->temps, g->num_temps * sizeof(MVMSpeshTemporary));
        g->temps = new_temps;
    }
}

/* Gets a temporary register of the specified kind to use in some transform.
 * Will only actually extend the frame if needed; if an existing temporary
 * was requested and then released, then it will just use a new version of
 * that. */
static void grow_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 orig) {
    MVMSpeshFacts *new_fact_row = MVM_spesh_alloc(tc, g,
        (g->fact_counts[orig] + 1) * sizeof(MVMSpeshFacts));
    memcpy(new_fact_row, g->facts[orig],
        g->fact_counts[orig] * sizeof(MVMSpeshFacts));
    g->facts[orig] = new_fact_row;
    g->fact_counts[orig]++;
}
static MVMSpeshOperand make_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 kind,
        MVMuint16 reuse) {
    MVMSpeshOperand   result;
    MVMSpeshFacts   **new_facts;
    MVMuint16        *new_fact_counts;
    MVMuint16         i;

    /* First, see if we can find an existing free temporary; use it if so. */
    if (reuse) {
        for (i = 0; i < g->num_temps; i++) {
            if (g->temps[i].kind == kind && !g->temps[i].in_use) {
                /* Add new facts slot. */
                MVMuint16 orig = g->temps[i].orig;
                MVMSpeshFacts *new_fact_row = MVM_spesh_alloc(tc, g,
                    (g->fact_counts[orig] + 1) * sizeof(MVMSpeshFacts));
                memcpy(new_fact_row, g->facts[orig],
                    g->fact_counts[orig] * sizeof(MVMSpeshFacts));
                g->facts[orig] = new_fact_row;
                g->fact_counts[orig]++;

                /* Mark it in use and add extra version. */
                g->temps[i].in_use++;
                g->temps[i].i++;

                /* Produce and return result. */
                /* Ensure that all the bits in the union are initialised.
                 * Code in `optimize_bb_switch` evaluates `lit_i64 != -1`
                 * before calling `MVM_spesh_manipulate_release_temp_reg` */
                result.lit_i64 = 0;
                result.reg.orig = orig;
                result.reg.i = g->temps[i].used_i = g->temps[i].i;
                return result;
            }
        }
    }

    /* Make sure we've space in the temporaries store. */
    ensure_more_temps(tc, g);

    /* Again, ensure that all the bits in the union are initialised. */
    result.lit_i64 = 0;

    /* Allocate temporary and set up result. */
    g->temps[g->num_temps].orig   = result.reg.orig = g->num_locals;
    g->temps[g->num_temps].i      = result.reg.i    = 0;
    g->temps[g->num_temps].used_i = 0;
    g->temps[g->num_temps].kind   = kind;
    g->temps[g->num_temps].in_use = 1;
    g->num_temps++;

    /* Add locals table entry. */
    if (!g->local_types) {
        MVMint32 local_types_size = g->num_locals * sizeof(MVMuint16);
        g->local_types = MVM_malloc(local_types_size);
        memcpy(g->local_types, g->sf->body.local_types, local_types_size);
    }
    g->local_types = MVM_realloc(g->local_types, (g->num_locals + 1) * sizeof(MVMuint16));
    g->local_types[g->num_locals] = kind;

    /* Add facts table entry. */
    new_facts       = MVM_spesh_alloc(tc, g, (g->num_locals + 1) * sizeof(MVMSpeshFacts *));
    new_fact_counts = MVM_spesh_alloc(tc, g, (g->num_locals + 1) * sizeof(MVMuint16));
    memcpy(new_facts, g->facts, g->num_locals * sizeof(MVMSpeshFacts *));
    memcpy(new_fact_counts, g->fact_counts, g->num_locals * sizeof(MVMuint16));
    new_facts[g->num_locals]       = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshFacts));
    new_fact_counts[g->num_locals] = 1;
    g->facts                       = new_facts;
    g->fact_counts                 = new_fact_counts;

    /* Increment number of locals. */
    g->num_locals++;

    return result;
}

/* Gets a temporary register, adding it to the set of registers of the
 * frame. */
MVMSpeshOperand MVM_spesh_manipulate_get_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 kind) {
    return make_temp_reg(tc, g, kind, 1);
}

/* Releases a temporary register, so it can be used again later. */
void MVM_spesh_manipulate_release_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand temp) {
    MVMuint16 i;
    for (i = 0; i < g->num_temps; i++) {
        if (g->temps[i].orig == temp.reg.orig && g->temps[i].used_i == temp.reg.i) {
            if (g->temps[i].in_use)
                g->temps[i].in_use = 0;
            else
                MVM_oops(tc, "Spesh: releasing temp not in use");
            return;
        }
    }
    MVM_oops(tc, "Spesh: releasing non-existing temp");
}

/* Gets a new SSA version of a register, allocating facts for it. Returns an
 * MVMSpeshOperand representing the new version along with the local it's a
 * version of. */
MVMSpeshOperand MVM_spesh_manipulate_new_version(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 orig) {
    MVMuint32 i;

    /* Grow the facts table to hold the new version, bumping the versions
     * count along the way. */
    MVMSpeshOperand result;
    result.reg.orig = orig;
    result.reg.i = g->fact_counts[orig];
    grow_facts(tc, g, orig);

    /* Check if it's a temp, and bump the temp count if so. */
    for (i = 0; i < g->num_temps; i++) {
        if (g->temps[i].orig == orig) {
            g->temps[i].i++;
            break;
        }
    }

    return result;
}

/* Performs an SSA version split at the specified instruction, such that the
 * reads of the SSA value dominated by (and including) the specified instruction
 * will use a new version. Returns the new version, which will at that point
 * lack a writer; a writer should be inserted for it. */
MVMSpeshOperand MVM_spesh_manipulate_split_version(MVMThreadContext *tc, MVMSpeshGraph *g,
                                                   MVMSpeshOperand split, MVMSpeshBB *bb,
                                                   MVMSpeshIns *at) {
    MVMSpeshOperand new_version = MVM_spesh_manipulate_new_version(tc, g, split.reg.orig);
    /* Queue of children to process; more than we need by definition */
    MVMSpeshBB **bbq = alloca(sizeof(MVMSpeshBB*) * g->num_bbs);
    MVMint32 top = 0;
    /* Push initial basic block */
    bbq[top++] = bb;
    while (top != 0) {
        /* Update instructions in this basic block. */
        MVMuint32 i;
        MVMSpeshBB *cur_bb = bbq[--top];
        MVMSpeshIns *ins = cur_bb == bb ? at : cur_bb->first_ins;
        while (ins) {
            for (i = 0; i < ins->info->num_operands; i++) {
                if ((ins->info->operands[i] & MVM_operand_rw_mask) == MVM_operand_read_reg) {
                    if (ins->operands[i].reg.orig == split.reg.orig &&
                            ins->operands[i].reg.i == split.reg.i) {
                        ins->operands[i] = new_version;
                        MVM_spesh_usages_delete_by_reg(tc, g, split, ins);
                        MVM_spesh_usages_add_by_reg(tc, g, new_version, ins);
                    }
                }
            }
            ins = ins->next;
        }

        /* Add dominance children to the queue. */
        for (i = 0; i < cur_bb->num_children; i++)
            bbq[top++] = cur_bb->children[i];
    }
    return new_version;
}

/* Gets a frame-unique register, adding it to the set of registers of the
 * frame. This does not hand back a particular version, it just selects the
 * unversioned register. */
MVMuint16 MVM_spesh_manipulate_get_unique_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 kind) {
    return make_temp_reg(tc, g, kind, 0).reg.orig;
}

/* Get the current version of an SSA temporary. */
MVMuint16 MVM_spesh_manipulate_get_current_version(MVMThreadContext *tc, MVMSpeshGraph *g,
        MVMuint16 orig) {
    MVMuint32 i;
    for (i = 0; i < g->num_temps; i++)
        if (g->temps[i].orig == orig)
            return g->temps[i].i;
    MVM_oops(tc, "Could not find register version for %d", orig);
}

MVMSpeshBB *MVM_spesh_manipulate_split_BB_at(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshBB *new_bb = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB));
    MVMSpeshBB *linear_next = bb->linear_next;

    /* Step one: insert the new BB into the linear order. */
    bb->linear_next = new_bb;
    new_bb->linear_next = linear_next;

    /* Step two: update all idx fields. */
    new_bb->idx = bb->idx + 1;
    {
        MVMSpeshBB *ptr = g->entry;
        while (ptr != NULL) {
            if (ptr != new_bb && ptr->idx > bb->idx)
                ptr->idx += 1;
            ptr = ptr->linear_next;
        }
    }

    /* Step three: fix up the dominator tree. */
    new_bb->children = bb->children;
    new_bb->num_children = bb->num_children;

    /* We expect the user of this API to fill whatever BB the code
     * will additionally branch into into the children list, as well.
     * Hopefully, setting num_children to 2 makes the code crash in case
     * that step has been forgotten. */
    bb->children = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *) * 2);
    bb->num_children = 2;
    bb->children[0] = new_bb;
    bb->children[1] = 0;

    /* Step three: fix up succs and preds. */
    new_bb->pred = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *));
    new_bb->num_pred = 1;
    new_bb->pred[0] = bb;

    new_bb->succ = bb->succ;
    new_bb->num_succ = bb->num_succ;

    for (MVMuint16 i = 0; i < new_bb->num_succ; i++) {
        MVMSpeshBB *succ = new_bb->succ[i];
        if (succ)
            for (MVMuint16 j = 0; j < succ->num_pred; j++)
                if (succ->pred[j] == bb)
                    succ->pred[j] = new_bb;
    }

    /* We assume the reason for the split is to add a new succ in the middle
     * which is why we allocate two slots instead of 1. */
    bb->succ = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *) * 2);
    bb->num_succ = 2;
    bb->succ[0] = new_bb;
    bb->succ[1] = 0;

    new_bb->initial_pc = bb->initial_pc;

    new_bb->num_df = 0;

    /* Update the books, since we now have more basic blocks in the graph. */
    g->num_bbs++;

    /* Last step: Transfer over the instructions after the split point. */
    new_bb->last_ins = bb->last_ins;
    bb->last_ins = ins->prev;
    new_bb->first_ins = ins;
    ins->prev->next = NULL;
    ins->prev = NULL;

    return new_bb;
}
