#include "moar.h"

/* This file contains various routines for manipulating a spesh graph, such
 * as adding/removing/replacing instructions. */

void MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *ins) {
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

void MVM_spesh_manipulate_remove_successor(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshBB *succ) {
    MVMuint16 i;
    MVMuint16 k;
    for (i = 0; i < bb->num_succ; i++) {
        if (bb->succ[i] == succ) {
            break;
        }
    }
    if (bb->succ[i] != succ) {
        MVM_exception_throw_adhoc(tc, "Didn't find the successor to remove from a Spesh Basic Block");
    }
    /* Remove the succ from the list, shuffle other successors back in place */
    for (k = i; k < bb->num_succ - 1; k++) {
        bb->succ[k] = bb->succ[k+1];
    }
    bb->succ[bb->num_succ - 1] = NULL;
    bb->num_succ--;

    /* We also need to remove the bb from the dominance children list */
    if (bb->children) {
        for (i = 0; i < bb->num_children; i++) {
            if (bb->children[i] == succ) {
                break;
            }
        }
        if (bb->children[i] == succ) {
            /* Remove the succ from the list, shuffle other successors back in place */
            for (k = i; k < bb->num_children - 1; k++) {
                bb->children[k] = bb->children[k+1];
            }
            bb->children[bb->num_children - 1] = NULL;
            bb->num_children--;
        }
    }

    /* Now hunt the bb in the succ's pred, so that we remove all traces of the connection */
    for (i = 0; i < succ->num_pred; i++) {
        if (succ->pred[i] == bb) {
            break;
        }
    }
    if (succ->pred[i] != bb) {
        MVM_exception_throw_adhoc(tc, "Didn't find the predecessor to remove from a Spesh Basic Block");
    }
    for (k = i; k < succ->num_pred - 1; k++) {
        succ->pred[k] = succ->pred[k+1];
    }
    succ->pred[succ->num_pred - 1] = NULL;
    succ->num_pred--;
}
