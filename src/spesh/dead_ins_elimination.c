#include "moar.h"

/* Eliminates any unused instructions. */
void MVM_spesh_eliminate_dead_ins(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Keep eliminating to a fixed point. */
    MVMint8 death = 1;
    while (death) {
        MVMSpeshBB *bb = g->entry;
        death = 0;
        while (bb) {
            MVMSpeshIns *ins = bb->last_ins;
            while (ins) {
                MVMSpeshIns *prev = ins->prev;
                if (ins->info->opcode == MVM_SSA_PHI) {
                    if (!MVM_spesh_usages_is_used(tc, g, ins->operands[0])) {
                        /* Remove this phi. */
                        MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                        death = 1;
                    }
                }
                else if (ins->info->pure) {
                    /* Sanity check to make sure it's a write reg as first operand. */
                    if ((ins->info->operands[0] & MVM_operand_rw_mask) == MVM_operand_write_reg) {
                        if (!MVM_spesh_usages_is_used(tc, g, ins->operands[0])) {
                            /* Remove this instruction. */
                            MVM_spesh_manipulate_delete_ins(tc, g, bb, ins);
                            death = 1;
                        }
                    }
                }
                ins = prev;
            }
            bb = bb->linear_next;
        }
    }
}
