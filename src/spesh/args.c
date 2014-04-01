#include "moar.h"

/* Maximum number of positional args we'll consider for optimization purposes. */
#define MAX_POS_ARGS 8

/* Takes information about the incoming callsite and arguments, and performs
 * various optimizations based on that information. */
void MVM_spesh_args(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCallsite *cs, MVMRegister *args) {
    /* We need to identify the various arg-related instructions in the graph,
     * then manipulate them as a whole. */
    MVMSpeshIns  *checkarity_ins     = NULL;
    MVMSpeshBB   *checkarity_bb      = NULL;
    MVMSpeshIns  *paramnamesused_ins = NULL;
    MVMSpeshBB   *paramnamesused_bb  = NULL;
    MVMSpeshIns **req_pos_ins        = calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **req_pos_bb         = calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMint32      req_max            = -1;

    /* Walk through the graph, looking for arg related instructions. */
    MVMSpeshBB *bb = g->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
            case MVM_OP_checkarity:
                if (checkarity_ins)
                    goto cleanup; /* Dupe; weird; bail out! */
                checkarity_ins = ins;
                checkarity_bb  = bb;
                break;
            case MVM_OP_param_rp_i:
            case MVM_OP_param_rp_n:
            case MVM_OP_param_rp_s:
            case MVM_OP_param_rp_o: {
                /* Required positional. */
                MVMint16 idx = ins->operands[1].lit_i16;
                if (idx < 0 || idx >= MAX_POS_ARGS)
                    goto cleanup;
                if (req_pos_ins[idx]) /* Dupe; weird. */
                    goto cleanup;
                req_pos_ins[idx] = ins;
                req_pos_bb[idx]  = bb;
                if (idx > req_max)
                    req_max = idx;
                break;
            }
            case MVM_OP_param_op_i:
            case MVM_OP_param_op_n:
            case MVM_OP_param_op_s:
            case MVM_OP_param_op_o:
            case MVM_OP_param_rn_i:
            case MVM_OP_param_rn_n:
            case MVM_OP_param_rn_s:
            case MVM_OP_param_rn_o:
            case MVM_OP_param_on_i:
            case MVM_OP_param_on_n:
            case MVM_OP_param_on_s:
            case MVM_OP_param_on_o:
            case MVM_OP_param_sp:
            case MVM_OP_param_sn:
                /* Don't know how to handle these yet; bail out. */
                goto cleanup;
            case MVM_OP_usecapture:
            case MVM_OP_savecapture:
                /* Require full args processing context for now; bail. */
                goto cleanup;
            case MVM_OP_paramnamesused:
                if (paramnamesused_ins)
                    goto cleanup; /* Dupe; weird; bail out! */
                paramnamesused_ins = ins;
                paramnamesused_bb  = bb;
                break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* If we didn't find a checkarity instruction, bail. */
    if (!checkarity_ins)
        goto cleanup;

    /* If required exactly matches the number of passed args... */
    if (req_max + 1 == cs->num_pos) {
        /* Ensure we've got all the arg fetch instructions we need, and that
         * types match. (TODO: insert box/unbox instructions.) */
        MVMint32 i;
        for (i = 0; i <= req_max; i++) {
            if (!req_pos_ins[i])
                goto cleanup;
            switch (req_pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_INT)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_n:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_NUM)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_s:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_STR)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_o:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_OBJ)
                    goto cleanup;
                break;
            }
        }

        /* We can optimize. Toss checkarity and paramnamesused. */
        MVM_spesh_manipulate_delete_ins(tc, checkarity_bb, checkarity_ins);
        if (paramnamesused_ins)
            MVM_spesh_manipulate_delete_ins(tc, paramnamesused_bb, paramnamesused_ins);

        /* Re-write the others to spesh ops. */
        for (i = 0; i <= req_max; i++) {
            switch (req_pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
                req_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                break;
            case MVM_OP_param_rp_n:
                req_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                break;
            case MVM_OP_param_rp_s:
                req_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                break;
            case MVM_OP_param_rp_o:
                req_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                break;
            }
            req_pos_ins[i]->operands[1].lit_i16 = (MVMint16)i;
        }
    }

  cleanup:
    free(req_pos_ins);
    free(req_pos_bb);
}
