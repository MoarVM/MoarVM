#include "moar.h"

/* This file contains various routines for manipulating a spesh graph, such
 * as adding/removing/replacing instructions. */

void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *ins) {
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

    /* Move it's annotations. */
    while (ins->annotations) {
        MVMSpeshAnn *ann      = ins->annotations;
        MVMSpeshAnn *ann_next = ann->next;
        switch (ann->type) {
            case MVM_SPESH_ANN_FH_START:
            case MVM_SPESH_ANN_FH_GOTO:
                if (next) {
                    ann->next = next->annotations;
                    next->annotations = ann;
                }
                break;
            case MVM_SPESH_ANN_FH_END:
            case MVM_SPESH_ANN_DEOPT_ONE_INS:
                if (prev) {
                    ann->next = prev->annotations;
                    prev->annotations = ann;
                }
                break;
        }
        ins->annotations = ann_next;
    }
}

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
        MVM_exception_throw_adhoc(tc, "Didn't find the successor to remove from a Spesh Basic Block");
    }

    /* Remove the succ from the list, shuffle other successors back in place */
    for (k = i; k < bb_num_succ; k++) {
        bb_succ[k] = bb_succ[k + 1];
    }

    bb_succ[bb_num_succ] = NULL;

    /* Now hunt the bb in the succ's pred, so that we remove all traces of the connection */
    for (i = 0; i <= succ_num_pred; i++) {
        if (succ_pred[i] == bb) {
            break;
        }
    }

    if (succ_pred[i] != bb) {
        MVM_exception_throw_adhoc(tc, "Didn't find the predecessor to remove from a Spesh Basic Block");
    }

    for (k = i; k < succ_num_pred; k++) {
        succ_pred[k] = succ_pred[k + 1];
    }

    succ_pred[succ_num_pred] = NULL;
}
