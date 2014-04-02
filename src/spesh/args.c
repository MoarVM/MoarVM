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
    MVMSpeshBB  **req_pos_bb         = calloc(MAX_POS_ARGS, sizeof(MVMSpeshBB *));
    MVMint32      req_max            = -1;

    MVMSpeshIns **opt_pos_ins        = calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **opt_pos_bb         = calloc(MAX_POS_ARGS, sizeof(MVMSpeshBB *));
    MVMint32      opt_max            = -1;

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
            case MVM_OP_param_op_o: {
                /* Optional Positional int/num/string/object */
                MVMint16 idx = ins->operands[1].lit_i16;
                if (idx < 0 || idx >= MAX_POS_ARGS)
                    goto cleanup;
                if (req_pos_ins[idx]) /* Dupe; weird. */
                    goto cleanup;
                opt_pos_ins[idx] = ins;
                opt_pos_bb[idx]  = bb;
                if (idx > req_max)
                    opt_max = idx;
                break;
            }
            case MVM_OP_param_on_i:
            case MVM_OP_param_on_n:
            case MVM_OP_param_on_s:
            case MVM_OP_param_on_o:
            case MVM_OP_param_rn_i:
            case MVM_OP_param_rn_n:
            case MVM_OP_param_rn_s:
            case MVM_OP_param_rn_o:
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

    /* If every required positional has been passed ... */
    if (cs->num_pos >= req_max + 1) {
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

        for (i = 0; i <= opt_max; i++) {
            MVMuint8 passed = i - (req_max + 1) < cs->num_pos;
            /* If we have reached the first optional parameter that was not
             * passed, we can stop checking the types. */
            if (!passed) {
                break;
            }
            if (!opt_pos_ins[i])
                goto cleanup;
            switch (opt_pos_ins[i]->info->opcode) {
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

        for (i = 0; i <= opt_max; i++) {
            MVMuint8 passed = i + (req_max + 1) < cs->num_pos;
            if (passed) {
                /* If we know the argument has been passed, we can pretend it's
                 * a required parameter instead */
                switch (opt_pos_ins[i]->info->opcode) {
                case MVM_OP_param_op_i:
                    opt_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                    break;
                case MVM_OP_param_op_n:
                    opt_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                    break;
                case MVM_OP_param_op_s:
                    opt_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                    break;
                case MVM_OP_param_op_o:
                    opt_pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    break;
                }

                MVMSpeshIns *inserted_goto = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
                MVMSpeshOperand *op = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ));
                inserted_goto->info = MVM_op_get_op(MVM_OP_goto);
                inserted_goto->operands = op;
                inserted_goto->annotations = NULL;

                op->ins_bb = opt_pos_ins[i]->operands[2].ins_bb;

                MVM_spesh_manipulate_insert_ins(tc, opt_pos_bb[i], opt_pos_ins[i], inserted_goto);

                /* Inserting an unconditional goto makes the linear_next BB
                 * unreachable, so we remove it from the succ list. */
                 MVM_spesh_manipulate_remove_successor(tc, opt_pos_bb[i], opt_pos_bb[i]->linear_next);
            } else {
                /* If we didn't pass this, just fall through the original
                 * operation and we'll get the default value set. */
                MVM_spesh_manipulate_remove_successor(tc, opt_pos_bb[i], opt_pos_ins[i]->operands[2].ins_bb);
                MVM_spesh_manipulate_delete_ins(tc, opt_pos_bb[i], opt_pos_ins[i]);
            }
        }
    }

  cleanup:
    free(req_pos_ins);
    free(req_pos_bb);
}
