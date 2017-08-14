#include "moar.h"

/* This file contains various routines for manipulating a spesh graph, such
 * as adding/removing/replacing instructions. */

/* Deletes an instruction, and does any fact changes as a result. */
void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshGraph *g,
                                     MVMSpeshBB *bb, MVMSpeshIns *ins) {
    /* Remove it from the double linked list. */
    MVMSpeshIns *prev = ins->prev;
    MVMSpeshIns *next = ins->next;
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
        switch (ann->type) {
            case MVM_SPESH_ANN_FH_START:
            case MVM_SPESH_ANN_FH_GOTO:
            case MVM_SPESH_ANN_INLINE_START:
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
                 * deopt to a later place than we should have. */
                if (!prev) {
                    MVMSpeshBB *prev_bb = MVM_spesh_graph_linear_prev(tc, g, bb);
                    while (prev_bb && !prev_bb->last_ins)
                        prev_bb = MVM_spesh_graph_linear_prev(tc, g, prev_bb);
                    if (prev_bb)
                        prev = prev_bb->last_ins;
                }
                if (prev) {
                    MVMSpeshAnn *append_to = prev->annotations;
                    while (append_to && append_to->next)
                        append_to = append_to->next;
                    append_to = ann;
                    ann->next = NULL;
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
    if (ins->info->opcode == MVM_SSA_PHI) {
        MVMint32 i;
        MVM_spesh_get_facts(tc, g, ins->operands[0])->dead_writer = 1;
        for (i = 1; i < ins->info->num_operands; i++)
            MVM_spesh_get_facts(tc, g, ins->operands[i])->usages--;
    }
    else {
        MVMint32 i;
        for (i = 0; i < ins->info->num_operands; i++) {
            MVMint32 rw = ins->info->operands[i] & MVM_operand_rw_mask;
            if (rw == MVM_operand_write_reg)
                MVM_spesh_get_facts(tc, g, ins->operands[i])->dead_writer = 1;
            else if (rw == MVM_operand_read_reg)
                MVM_spesh_get_facts(tc, g, ins->operands[i])->usages--;
        }
    }
}

/* Inserts an instruction after the specified instruciton, or at the start of
 * the basic block if the instruction is NULL. */
void MVM_spesh_manipulate_insert_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *previous, MVMSpeshIns *to_insert) {
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

/* Gets a temporary register of the specified kind to use in some transform.
 * Will only actually extend the frame if needed; if an existing temporary
 * was requested and then released, then it will just use a new version of
 * that. */
MVMSpeshOperand MVM_spesh_manipulate_get_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMuint16 kind) {
    MVMSpeshOperand   result;
    MVMSpeshFacts   **new_facts;
    MVMuint16        *new_fact_counts;
    MVMuint16         i;

    /* First, see if we can find an existing free temporary; use it if so. */
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
            result.reg.orig = orig;
            result.reg.i    = g->temps[i].i;
            return result;
        }
    }

    /* Make sure we've space in the temporaries store. */
    if (g->num_temps == g->alloc_temps) {
        MVMSpeshTemporary *new_temps;
        g->alloc_temps += 4;
        new_temps = MVM_spesh_alloc(tc, g, g->alloc_temps * sizeof(MVMSpeshTemporary));
        if (g->num_temps)
            memcpy(new_temps, g->temps, g->num_temps * sizeof(MVMSpeshTemporary));
        g->temps = new_temps;
    }

    /* Allocate temporary and set up result. */
    g->temps[g->num_temps].orig   = result.reg.orig = g->num_locals;
    g->temps[g->num_temps].i      = result.reg.i    = 0;
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

/* Releases a temporary register, so it can be used again later. */
void MVM_spesh_manipulate_release_temp_reg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshOperand temp) {
    MVMuint16 i;
    for (i = 0; i < g->num_temps; i++) {
        if (g->temps[i].orig == temp.reg.orig && g->temps[i].i == temp.reg.i) {
            if (g->temps[i].in_use)
                g->temps[i].in_use = 0;
            else
                MVM_oops(tc, "Spesh: releasing temp not in use");
            return;
        }
    }
    MVM_oops(tc, "Spesh: releasing non-existing temp");
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
        MVMSpeshBB *ptr = linear_next;
        while (ptr != NULL) {
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

    /* We assume the reason for the split is to add a new succ in the middle
     * which is why we allocate two slots instead of 1. */
    bb->succ = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshBB *) * 2);
    bb->num_succ = 2;
    bb->succ[0] = new_bb;
    bb->succ[1] = 0;

    new_bb->initial_pc = bb->initial_pc;

    new_bb->num_df = 0;

    /* Last step: Transfer over the instructions after the split point. */
    new_bb->last_ins = bb->last_ins;
    bb->last_ins = ins->prev;
    new_bb->first_ins = ins;
    ins->prev->next = NULL;
    ins->prev = NULL;

    return new_bb;
}
