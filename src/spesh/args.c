#include "moar.h"

/* Maximum number of positional args we'll consider for optimization purposes. */
#define MAX_POS_ARGS 8

/* Maximum number of named args we'll consider for optimization purposes. */
#define MAX_NAMED_ARGS 8

/* Adds guards and facts for an object arg. */
static void add_guards_and_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 slot,
                          MVMObject *arg, MVMSpeshIns *arg_ins) {
    /* Grab type and concreteness. */
    MVMObject *type     = STABLE(arg)->WHAT;
    MVMint32   concrete = IS_CONCRETE(arg);
    MVMint32   is_cont  = 0;

    /* Add appropriate facts from arg itself. */
    MVMint16 orig = arg_ins->operands[0].reg.orig;
    MVMint16 i    = arg_ins->operands[0].reg.i;
    g->facts[orig][i].type   = type;
    g->facts[orig][i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;
    if (concrete) {
        g->facts[orig][i].flags |= MVM_SPESH_FACT_CONCRETE;
        if (!STABLE(type)->container_spec)
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONTED;
        else
            is_cont = 1;
    }
    else {
        g->facts[orig][i].flags |= MVM_SPESH_FACT_TYPEOBJ | MVM_SPESH_FACT_DECONTED;
    }

    /* Add guard record for the arg type. */
    g->arg_guards[g->num_arg_guards].slot  = slot;
    g->arg_guards[g->num_arg_guards].match = (MVMCollectable *)STABLE(type);
    if (concrete)
        g->arg_guards[g->num_arg_guards].kind = MVM_SPESH_GUARD_CONC;
    else
        g->arg_guards[g->num_arg_guards].kind = MVM_SPESH_GUARD_TYPE;
    g->num_arg_guards++;

    /* If we know it's a container, might be able to look inside it to
     * further optimize. */
    if (is_cont && STABLE(type)->container_spec->fetch_never_invokes) {
        /* Fetch argument from the container. */
        MVMRegister r;
        STABLE(type)->container_spec->fetch(tc, arg, &r);
        arg = r.o;
        if (!arg)
            return;

        /* Add facts about it. */
        type                           = STABLE(arg)->WHAT;
        concrete                       = IS_CONCRETE(arg);
        g->facts[orig][i].decont_type  = type;
        g->facts[orig][i].flags       |= MVM_SPESH_FACT_KNOWN_DECONT_TYPE;
        if (concrete)
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONT_CONCRETE;
        else
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONT_TYPEOBJ;

        /* Add guard for contained value. */
        g->arg_guards[g->num_arg_guards].slot  = slot;
        g->arg_guards[g->num_arg_guards].match = (MVMCollectable *)STABLE(type);
        if (concrete)
            g->arg_guards[g->num_arg_guards].kind = MVM_SPESH_GUARD_DC_CONC;
        else
            g->arg_guards[g->num_arg_guards].kind = MVM_SPESH_GUARD_DC_TYPE;
        g->num_arg_guards++;
    }
}

/* Adds an instruction marking a name arg as being used (if we turned its
 * fetching into a positional). */
static MVMSpeshIns * add_named_used_ins(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                               MVMSpeshIns *ins, MVMint32 idx) {
    MVMSpeshIns *inserted_ins = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshIns ));
    MVMSpeshOperand *operands = MVM_spesh_alloc(tc, g, sizeof( MVMSpeshOperand ));
    inserted_ins->info        = MVM_op_get_op(MVM_OP_sp_namedarg_used);
    inserted_ins->operands    = operands;
    operands[0].lit_i16       = (MVMint16)idx;
    MVM_spesh_manipulate_insert_ins(tc, bb, ins, inserted_ins);
    return inserted_ins;
}

/* Handles a pos arg that needs unboxing. */
static void pos_unbox(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                      MVMSpeshIns *ins, const MVMOpInfo *unbox_op) {
    MVMSpeshOperand  temp  = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
    MVMSpeshIns     *unbox = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    unbox->info            = unbox_op;
    unbox->operands        = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    unbox->operands[0]     = ins->operands[0];
    unbox->operands[1]     = temp;
    ins->info              = MVM_op_get_op(MVM_OP_sp_getarg_o);
    ins->operands[0]       = temp;
    MVM_spesh_manipulate_insert_ins(tc, bb, ins, unbox);
    MVM_spesh_manipulate_release_temp_reg(tc, g, temp);
}

/* Handles a pos arg that needs boxing. */
static void pos_box(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                    MVMSpeshIns *ins, const MVMOpInfo *hlltype_op, const MVMOpInfo *box_op,
                    const MVMOpInfo *arg_op, MVMuint8 kind) {
    MVMSpeshOperand  temp_bt, temp_arg;
    MVMSpeshIns     *hlltype, *box;

    /* Add HLL type op. */
    temp_bt              = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
    hlltype              = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    hlltype->info        = hlltype_op;
    hlltype->operands    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
    hlltype->operands[0] = temp_bt;
    MVM_spesh_manipulate_insert_ins(tc, bb, ins, hlltype);

    /* Add box op. */
    temp_arg         = MVM_spesh_manipulate_get_temp_reg(tc, g, kind);
    box              = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    box->info        = box_op;
    box->operands    = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    box->operands[0] = ins->operands[0];
    box->operands[1] = temp_arg;
    box->operands[2] = temp_bt;
    MVM_spesh_manipulate_insert_ins(tc, bb, hlltype, box);

    /* Update instruction to receive unboxed arg. */
    ins->info        = arg_op;
    ins->operands[0] = temp_arg;

    /* Release temporary registers. */
    MVM_spesh_manipulate_release_temp_reg(tc, g, temp_bt);
    MVM_spesh_manipulate_release_temp_reg(tc, g, temp_arg);
}

/* Gets the primitive boxed by a type. */
static MVMuint16 prim_spec(MVMThreadContext *tc, MVMObject *type) {
    return type
        ? REPR(type)->get_storage_spec(tc, STABLE(type))->boxed_primitive
        : 0;
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
    MVMSpeshIns  *param_sn_ins       = NULL;

    MVMSpeshIns **pos_ins    = MVM_calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **pos_bb     = MVM_calloc(MAX_POS_ARGS, sizeof(MVMSpeshBB *));
    MVMuint8     *pos_added  = MVM_calloc(MAX_POS_ARGS, sizeof(MVMuint8));
    MVMSpeshIns **named_ins  = MVM_calloc(MAX_NAMED_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **named_bb   = MVM_calloc(MAX_NAMED_ARGS, sizeof(MVMSpeshBB *));
    MVMSpeshIns **used_ins   = MVM_calloc(MAX_NAMED_ARGS, sizeof(MVMSpeshIns *));
    MVMint32      req_max    = -1;
    MVMint32      opt_min    = -1;
    MVMint32      opt_max    = -1;
    MVMint32      num_named  = 0;
    MVMint32      named_used = 0;
    MVMint32      got_named  = cs->num_pos != cs->arg_count;

    MVMSpeshBB *bb = g->entry;

    g->cs = cs;

    /* Walk through the graph, looking for arg related instructions. */
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
                /* Named (optional or required). */
                if (num_named == MAX_NAMED_ARGS)
                    goto cleanup;
                named_ins[num_named] = ins;
                named_bb[num_named]  = bb;
                num_named++;
                break;
            case MVM_OP_param_sp:
                break;
            case MVM_OP_param_sn:
                param_sn_ins = ins;
                break;
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
         * types match or it's a box/unbox. */
        MVMint32 i;
        for (i = 0; i < cs->num_pos; i++) {
            MVMCallsiteEntry arg_flag = cs->arg_flags[i];
            if (!pos_ins[i])
                goto cleanup;
            switch (pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
            case MVM_OP_param_op_i:
                if (arg_flag != MVM_CALLSITE_ARG_INT)
                    if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                            prim_spec(tc, args[i].o) != MVM_STORAGE_SPEC_BP_INT)
                        goto cleanup;
                break;
            case MVM_OP_param_rp_n:
            case MVM_OP_param_op_n:
                if (arg_flag != MVM_CALLSITE_ARG_NUM)
                    if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                            prim_spec(tc, args[i].o) != MVM_STORAGE_SPEC_BP_NUM)
                        goto cleanup;
                break;
            case MVM_OP_param_rp_s:
            case MVM_OP_param_op_s:
                if (arg_flag != MVM_CALLSITE_ARG_STR)
                    if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                            prim_spec(tc, args[i].o) != MVM_STORAGE_SPEC_BP_STR)
                        goto cleanup;
                break;
            case MVM_OP_param_rp_o:
            case MVM_OP_param_op_o:
                if (arg_flag != MVM_CALLSITE_ARG_OBJ && arg_flag != MVM_CALLSITE_ARG_INT &&
                    arg_flag != MVM_CALLSITE_ARG_NUM && arg_flag != MVM_CALLSITE_ARG_STR)
                    goto cleanup;
                break;
            }
        }

        /* If we know there's no incoming nameds we can always turn param_sn into a
         * simple hash creation. This will typically be further lowered in optimize. */
        if (param_sn_ins && !got_named) {
            MVMObject *hash_type = g->sf->body.cu->body.hll_config->slurpy_hash_type;
            if (REPR(hash_type)->ID == MVM_REPR_ID_MVMHash) {
                MVMSpeshOperand target    = param_sn_ins->operands[0];
                param_sn_ins->info        = MVM_op_get_op(MVM_OP_sp_fastcreate);
                param_sn_ins->operands    = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                param_sn_ins->operands[0] = target;
                param_sn_ins->operands[1].lit_i16 = sizeof(MVMHash);
                param_sn_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g,
                    (MVMCollectable *)STABLE(hash_type));
            }
            else {
                goto cleanup;
            }
        }

        /* We can optimize. Toss checkarity. */
        MVM_spesh_manipulate_delete_ins(tc, g, checkarity_bb, checkarity_ins);

        /* Re-write the passed required positionals to spesh ops, and store
         * any gurads. */
        if (cs->arg_count)
            g->arg_guards = MVM_malloc(2 * cs->arg_count * sizeof(MVMSpeshGuard));
        for (i = 0; i < cs->num_pos; i++) {
            MVMCallsiteEntry arg_flag = cs->arg_flags[i];
            switch (pos_ins[i]->info->opcode) {
            case MVM_OP_param_rp_i:
            case MVM_OP_param_op_i:
                if (arg_flag == MVM_CALLSITE_ARG_INT) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_unbox_i));
                    pos_added[i]++;
                    if (args[i].o)
                        add_guards_and_facts(tc, g, i, args[i].o, pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_n:
            case MVM_OP_param_op_n:
                if (arg_flag == MVM_CALLSITE_ARG_NUM) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_unbox_n));
                    pos_added[i]++;
                    if (args[i].o)
                        add_guards_and_facts(tc, g, i, args[i].o, pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_s:
            case MVM_OP_param_op_s:
                if (arg_flag == MVM_CALLSITE_ARG_STR) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_unbox_s));
                    pos_added[i]++;
                    if (args[i].o)
                        add_guards_and_facts(tc, g, i, args[i].o, pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_o:
            case MVM_OP_param_op_o:
                if (arg_flag == MVM_CALLSITE_ARG_OBJ) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    if (args[i].o)
                        add_guards_and_facts(tc, g, i, args[i].o, pos_ins[i]);
                }
                else if (arg_flag == MVM_CALLSITE_ARG_INT) {
                    pos_box(tc, g, pos_bb[i], pos_ins[i],
                        MVM_op_get_op(MVM_OP_hllboxtype_i), MVM_op_get_op(MVM_OP_box_i),
                        MVM_op_get_op(MVM_OP_sp_getarg_i), MVM_reg_int64);
                    pos_added[i] += 2;
                }
                else if (arg_flag == MVM_CALLSITE_ARG_NUM) {
                    pos_box(tc, g, pos_bb[i], pos_ins[i],
                        MVM_op_get_op(MVM_OP_hllboxtype_n), MVM_op_get_op(MVM_OP_box_n),
                        MVM_op_get_op(MVM_OP_sp_getarg_n), MVM_reg_num64);
                    pos_added[i] += 2;
                }
                else if (arg_flag == MVM_CALLSITE_ARG_STR) {
                    pos_box(tc, g, pos_bb[i], pos_ins[i],
                        MVM_op_get_op(MVM_OP_hllboxtype_s), MVM_op_get_op(MVM_OP_box_s),
                        MVM_op_get_op(MVM_OP_sp_getarg_s), MVM_reg_str);
                    pos_added[i] += 2;
                }
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
                    MVMSpeshIns *after = pos_ins[i];
                    while (pos_added[i]--)
                        after = after->next;
                    MVM_spesh_manipulate_insert_goto(tc, g, pos_bb[i], after,
                        pos_ins[i]->operands[2].ins_bb);

                    /* Inserting an unconditional goto makes the linear_next BB
                    * unreachable, so we remove it from the succ list. */
                    MVM_spesh_manipulate_remove_successor(tc, pos_bb[i],
                        pos_bb[i]->linear_next);
                } else {
                    /* If we didn't pass this, just fall through the original
                    * operation and we'll get the default value set. */
                    MVM_spesh_manipulate_delete_ins(tc, g, pos_bb[i], pos_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, pos_bb[i],
                        pos_ins[i]->operands[2].ins_bb);
                }
            }
        }

        /* Now consider any nameds. */
        for (i = 0; i < num_named; i++) {
            /* See if the arg was passed. */
            MVMString *arg_name      = MVM_spesh_get_string(tc, g, named_ins[i]->operands[1]);
            MVMint32   passed_nameds = (cs->arg_count - cs->num_pos) / 2;
            MVMint32   cs_flags      = cs->num_pos + passed_nameds;
            MVMint32   cur_idx       = 0;
            MVMint32   cur_named     = 0;
            MVMuint8   found_flag    = 0;
            MVMint32   found_idx     = -1;
            MVMint32   j;
            for (j = 0; j < cs_flags; j++) {
                if (cs->arg_flags[j] & MVM_CALLSITE_ARG_NAMED) {
                    if (MVM_string_equal(tc, arg_name, cs->arg_names[cur_named])) {
                        /* Found it. */
                        found_flag = cs->arg_flags[j];
                        found_idx  = cur_idx;
                        break;
                    }
                    cur_idx += 2;
                    cur_named++;
                }
                else {
                    cur_idx++;
                }
            }

            /* Now go by instruction. */
            switch (named_ins[i]->info->opcode) {
            case MVM_OP_param_rn_i:
                if (found_idx == -1)
                    goto cleanup;
                if (found_flag & MVM_CALLSITE_ARG_INT) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_INT) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_i));
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                }
                named_used++;
                break;
            case MVM_OP_param_rn_n:
                if (found_idx == -1)
                    goto cleanup;
                if (found_flag & MVM_CALLSITE_ARG_NUM) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_NUM) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_n));
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                }
                named_used++;
                break;
            case MVM_OP_param_rn_s:
                if (found_idx == -1)
                    goto cleanup;
                if (found_flag & MVM_CALLSITE_ARG_STR) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_STR) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_s));
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                }
                named_used++;
                break;
            case MVM_OP_param_rn_o:
                if (found_idx == -1)
                    goto cleanup;
                if (found_flag & MVM_CALLSITE_ARG_OBJ) {
                    MVMuint16 arg_idx = found_idx + 1;
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    named_ins[i]->operands[1].lit_i16 = arg_idx;
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                    if (args[arg_idx].o)
                        add_guards_and_facts(tc, g, arg_idx, args[arg_idx].o, named_ins[i]);
                }
                else if (found_flag & (MVM_CALLSITE_ARG_INT | MVM_CALLSITE_ARG_NUM | MVM_CALLSITE_ARG_STR)) {
                    MVMuint16 arg_idx = found_idx + 1;
                    named_ins[i]->operands[1].lit_i16 = arg_idx;
                    if (found_flag & MVM_CALLSITE_ARG_INT)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_i), MVM_op_get_op(MVM_OP_box_i),
                            MVM_op_get_op(MVM_OP_sp_getarg_i), MVM_reg_int64);
                    else if (found_flag & MVM_CALLSITE_ARG_NUM)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_n), MVM_op_get_op(MVM_OP_box_n),
                            MVM_op_get_op(MVM_OP_sp_getarg_n), MVM_reg_num64);
                    else if (found_flag & MVM_CALLSITE_ARG_STR)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_s), MVM_op_get_op(MVM_OP_box_s),
                            MVM_op_get_op(MVM_OP_sp_getarg_s), MVM_reg_str);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next->next, cur_named);
                }
                named_used++;
                break;
            case MVM_OP_param_on_i:
                if (found_idx == -1) {
                    MVM_spesh_manipulate_delete_ins(tc, g, named_bb[i], named_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i], named_ins[i]->operands[2].ins_bb);
                }
                else if (found_flag & MVM_CALLSITE_ARG_INT) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i],
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_INT) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_i));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                    named_used++;
                }
                break;
            case MVM_OP_param_on_n:
                if (found_idx == -1) {
                    MVM_spesh_manipulate_delete_ins(tc, g, named_bb[i], named_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i], named_ins[i]->operands[2].ins_bb);
                }
                else if (found_flag & MVM_CALLSITE_ARG_NUM) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i],
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_NUM) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_n));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                    named_used++;
                }
                break;
            case MVM_OP_param_on_s:
                if (found_idx == -1) {
                    MVM_spesh_manipulate_delete_ins(tc, g, named_bb[i], named_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i], named_ins[i]->operands[2].ins_bb);
                }
                else if (found_flag & MVM_CALLSITE_ARG_STR) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i],
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, args[found_idx].o) == MVM_STORAGE_SPEC_BP_STR) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_unbox_s));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next, cur_named);
                    named_used++;
                }
                break;
            case MVM_OP_param_on_o:
                if (found_idx == -1) {
                    MVM_spesh_manipulate_delete_ins(tc, g, named_bb[i], named_ins[i]);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i], named_ins[i]->operands[2].ins_bb);
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ) {
                    MVMuint16 arg_idx = found_idx + 1;
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    named_ins[i]->operands[1].lit_i16 = arg_idx;
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i],
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i], cur_named);
                    if (args[arg_idx].o)
                        add_guards_and_facts(tc, g, arg_idx, args[arg_idx].o, named_ins[i]);
                    named_used++;
                }
                else if (found_flag & (MVM_CALLSITE_ARG_INT | MVM_CALLSITE_ARG_NUM | MVM_CALLSITE_ARG_STR)) {
                    MVMuint16 arg_idx = found_idx + 1;
                    named_ins[i]->operands[1].lit_i16 = arg_idx;
                    if (found_flag & MVM_CALLSITE_ARG_INT)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_i), MVM_op_get_op(MVM_OP_box_i),
                            MVM_op_get_op(MVM_OP_sp_getarg_i), MVM_reg_int64);
                    else if (found_flag & MVM_CALLSITE_ARG_NUM)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_n), MVM_op_get_op(MVM_OP_box_n),
                            MVM_op_get_op(MVM_OP_sp_getarg_n), MVM_reg_num64);
                    else if (found_flag & MVM_CALLSITE_ARG_STR)
                        pos_box(tc, g, named_bb[i], named_ins[i],
                            MVM_op_get_op(MVM_OP_hllboxtype_s), MVM_op_get_op(MVM_OP_box_s),
                            MVM_op_get_op(MVM_OP_sp_getarg_s), MVM_reg_str);
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next->next,
                        named_ins[i]->operands[2].ins_bb);
                    used_ins[i] = add_named_used_ins(tc, g, named_bb[i], named_ins[i]->next->next, cur_named);
                    named_used++;
                }
                break;
            }
        }

        /* If we had no nameds or we used them all, can toss namesused, and we
         * don't need to mark used after all. */
        if (paramnamesused_ins && num_named == named_used) {
            MVM_spesh_manipulate_delete_ins(tc, g, paramnamesused_bb, paramnamesused_ins);
            for (i = 0; i < num_named; i++)
                if (used_ins[i])
                    MVM_spesh_manipulate_delete_ins(tc, g, named_bb[i], used_ins[i]);
        }
    }

  cleanup:
    MVM_free(pos_ins);
    MVM_free(pos_bb);
    MVM_free(pos_added);
    MVM_free(named_ins);
    MVM_free(named_bb);
    MVM_free(used_ins);
}
