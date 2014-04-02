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
