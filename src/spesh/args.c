#include "moar.h"

/* Maximum number of positional args we'll consider for optimization purposes. */
#define MAX_POS_ARGS 8

/* Adds guards and facts for an object arg. */
void add_guards_and_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 slot,
                          MVMObject *arg, MVMSpeshIns *arg_ins) {
    /* Grab type and concreteness. */
    MVMObject *type     = STABLE(arg)->WHAT;
    MVMint32   concrete = IS_CONCRETE(arg);

    /* Add appropriate facts. */
    MVMint16 orig = arg_ins->operands[0].reg.orig;
    MVMint16 i    = arg_ins->operands[0].reg.i;
    g->facts[orig][i].type   = type;
    g->facts[orig][i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;
    if (concrete) {
        g->facts[orig][i].flags |= MVM_SPESH_FACT_CONCRETE;
        if (!STABLE(type)->container_spec)
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONTED;
    }
    else {
        g->facts[orig][i].flags |= MVM_SPESH_FACT_TYPEOBJ | MVM_SPESH_FACT_DECONTED;
    }

    /* Add guard record. */
    g->guards[g->num_guards].slot  = slot;
    g->guards[g->num_guards].match = (MVMCollectable *)STABLE(type);
    if (concrete)
        g->guards[g->num_guards].kind = MVM_SPESH_GUARD_CONC;
    else
        g->guards[g->num_guards].kind = MVM_SPESH_GUARD_TYPE;
    g->num_guards++;
}

/* Takes information about the incoming callsite and arguments, and performs
 * various optimizations based on that information. */
void MVM_spesh_args(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCallsite *cs, MVMRegister *args) {
    /* We need to identify the various arg-related instructions in the graph,
     * then manipulate them as a whole. */
    MVMSpeshIns  *checkarity_ins     = NULL;
    MVMSpeshBB   *checkarity_bb      = NULL;
    MVMSpeshIns  *paramnamesused_ins = NULL;
    MVMSpeshBB   *paramnamesused_bb  = NULL;

    MVMSpeshIns **pos_ins = calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **pos_bb  = calloc(MAX_POS_ARGS, sizeof(MVMSpeshBB *));
    MVMint32      req_max = -1;
    MVMint32      opt_min = -1;
    MVMint32      opt_max = -1;

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
                if (pos_ins[idx]) /* Dupe; weird. */
                    goto cleanup;
                pos_ins[idx] = ins;
                pos_bb[idx]  = bb;
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
                if (pos_ins[idx]) /* Dupe; weird. */
                    goto cleanup;
                pos_ins[idx] = ins;
                pos_bb[idx]  = bb;
                if (idx > opt_max)
                    opt_max = idx;
                if (opt_min == -1 || idx < opt_min)
                    opt_min = idx;
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

    /* If required and optional aren't contiguous, bail. */
    if (opt_min >= 0 && req_max + 1 != opt_min)
        goto cleanup;

    /* If the number of passed args is in range... */
    if (cs->num_pos >= req_max + 1 && (opt_max < 0 || cs->num_pos <= opt_max + 1)) {
        /* Ensure we've got all the arg fetch instructions we need, and that
         * types match. (TODO: insert box/unbox instructions.) */
        MVMint32 i;
        for (i = 0; i < cs->num_pos; i++) {
            if (!pos_ins[i])
                goto cleanup;
            switch (pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
            case MVM_OP_param_op_i:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_INT)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_n:
            case MVM_OP_param_op_n:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_NUM)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_s:
            case MVM_OP_param_op_s:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_STR)
                    goto cleanup;
                break;
            case MVM_OP_param_rp_o:
            case MVM_OP_param_op_o:
                if (cs->arg_flags[i] != MVM_CALLSITE_ARG_OBJ)
                    goto cleanup;
                break;
            }
        }

        /* We can optimize. Toss checkarity and paramnamesused. */
        MVM_spesh_manipulate_delete_ins(tc, checkarity_bb, checkarity_ins);
        if (paramnamesused_ins)
            MVM_spesh_manipulate_delete_ins(tc, paramnamesused_bb, paramnamesused_ins);

        /* Re-write the passed things to spesh ops, and store any gurads. */
        if (cs->num_pos)
            g->guards = malloc(cs->num_pos * sizeof(MVMSpeshGuard));
        for (i = 0; i < cs->num_pos; i++) {
            switch (pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
            case MVM_OP_param_op_i:
                pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                break;
            case MVM_OP_param_rp_n:
            case MVM_OP_param_op_n:
                pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                break;
            case MVM_OP_param_rp_s:
            case MVM_OP_param_op_s:
                pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                break;
            case MVM_OP_param_rp_o:
            case MVM_OP_param_op_o:
                pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                if (args[i].o)
                    add_guards_and_facts(tc, g, i, args[i].o, pos_ins[i]);
                break;
            }
            pos_ins[i]->operands[1].lit_i16 = (MVMint16)i;
        }

        /* Now consider any optionals. */
        if (opt_min >= 0) {
            for (i = opt_min; i <= opt_max; i++) {
                MVMuint8 passed = i < cs->num_pos;
                if (passed) {
                    /* If we know the argument has been passed, then add a goto
                    * to the "passed" code. */
                    MVMSpeshIns *inserted_goto = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
                    MVMSpeshOperand *operands  = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ));
                    inserted_goto->info        = MVM_op_get_op(MVM_OP_goto);
                    inserted_goto->operands    = operands;
                    operands[0].ins_bb         = pos_ins[i]->operands[2].ins_bb;
                    MVM_spesh_manipulate_insert_ins(tc, pos_bb[i], pos_ins[i], inserted_goto);
    
                    /* Inserting an unconditional goto makes the linear_next BB
                    * unreachable, so we remove it from the succ list. */
                    MVM_spesh_manipulate_remove_successor(tc, pos_bb[i],
                        pos_bb[i]->linear_next);
                } else {
                    /* If we didn't pass this, just fall through the original
                    * operation and we'll get the default value set. */
                    MVM_spesh_manipulate_delete_ins(tc, pos_bb[i], pos_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, pos_bb[i],
                        pos_ins[i]->operands[2].ins_bb);
                }
            }
        }
    }

  cleanup:
    free(pos_ins);
    free(pos_bb);
}
