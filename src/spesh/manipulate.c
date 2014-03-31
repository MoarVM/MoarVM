#include "moar.h"

/* This file contains various routines for manipulating a spesh graph, such
 * as adding/removing/replacing instructions. */

MVM_spesh_manipulate_delete_ins(MVMThreadContext *tc, MVMSpeshBB *bb, MVMSpeshIns *ins) {
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
