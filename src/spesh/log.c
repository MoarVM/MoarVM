#include "moar.h"

/* In order to collect more information to use during specialization, we
 * make a pass through the code inserting logging instructions after a
 * range of insturctions that obtain data we can't reason about easily
 * statically. After a number of logging runs, the collected data is
 * used as an additional "fact" source while specializing. */

static void insert_log(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshIns *log_ins         = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    log_ins->info                = MVM_op_get_op(MVM_OP_sp_log);
    log_ins->operands            = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    log_ins->operands[0].reg     = ins->operands[0].reg;
    log_ins->operands[1].lit_i16 = g->num_log_slots;
    MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);
    g->num_log_slots++;
}
 
void MVM_spesh_log_add_logging(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB  *bb;

    /* We've no log slots so far. */
    g->num_log_slots = 0;

    /* Work through the code, adding logging instructions where needed. */
    bb = g->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
            case MVM_OP_getlex:
                if (g->sf->body.local_types[ins->operands[0].reg.orig] == MVM_reg_obj)
                    insert_log(tc, g, bb, ins);
                break;
            case MVM_OP_getlex_no:
            case MVM_OP_invoke_o:
            case MVM_OP_getattr_o:
            case MVM_OP_getattrs_o:
            case MVM_OP_getlexstatic_o:
            case MVM_OP_getlexperinvtype_o:
                insert_log(tc, g, bb, ins);
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* Allocate space for logging storage. */
    g->log_slots = g->num_log_slots
        ? calloc(g->num_log_slots * MVM_SPESH_LOG_RUNS, sizeof(MVMCollectable *))
        : NULL;
}
