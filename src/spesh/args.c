#include "moar.h"

/* Maximum number of positional args we'll consider for optimization purposes. */
#define MAX_POS_ARGS 8

/* Maximum number of named args we'll consider for optimization purposes. */
#define MAX_NAMED_ARGS 8

/* Adds facts for an object arg. */
static void add_facts(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 slot,
                      MVMSpeshStatsType type_tuple_entry, MVMSpeshIns *arg_ins) {
    /* Add appropriate facts from the arg type tuple. */
    MVMint16 orig = arg_ins->operands[0].reg.orig;
    MVMint16 i = arg_ins->operands[0].reg.i;
    MVMObject *type = type_tuple_entry.type;
    g->facts[orig][i].type = type;
    g->facts[orig][i].flags |= MVM_SPESH_FACT_KNOWN_TYPE;
    if (type_tuple_entry.type_concrete)
        g->facts[orig][i].flags |= MVM_SPESH_FACT_CONCRETE;
    else
        g->facts[orig][i].flags |= MVM_SPESH_FACT_TYPEOBJ;

    /* Add any decontainerized type info. */
    if (type_tuple_entry.decont_type) {
        g->facts[orig][i].decont_type  = type_tuple_entry.decont_type;
        g->facts[orig][i].flags       |= MVM_SPESH_FACT_KNOWN_DECONT_TYPE;
        if (type_tuple_entry.decont_type_concrete)
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONT_CONCRETE;
        else
            g->facts[orig][i].flags |= MVM_SPESH_FACT_DECONT_TYPEOBJ;
        if (type_tuple_entry.rw_cont)
            g->facts[orig][i].flags |= MVM_SPESH_FACT_RW_CONT;
    }
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
static MVMuint16 prim_spec(MVMThreadContext *tc, MVMSpeshStatsType *type_tuple, MVMint32 i) {
    MVMObject *type = type_tuple ? type_tuple[i].type : NULL;
    const MVMStorageSpec *ss;
    if (!type)
        return 0;
    ss = REPR(type)->get_storage_spec(tc, STABLE(type));
    if (ss->boxed_primitive)
        return ss->boxed_primitive;
    return 0;
}

static MVMuint8 cmp_prim_spec(MVMThreadContext *tc, MVMSpeshStatsType *type_tuple, MVMint32 i, MVMint32 wanted_prim_spec) {
    MVMObject *type = NULL;
    MVMint32 concrete = 0;
    const MVMStorageSpec *ss;
    if (type_tuple) {
        if (type_tuple[i].decont_type) {
            type = type_tuple[i].decont_type;
            concrete = type_tuple[i].decont_type_concrete;
        }
        else {
            type = type_tuple[i].type;
            concrete = type_tuple[i].type_concrete;
        }
    }
    if (!type || !concrete)
        return 0;
    ss = REPR(type)->get_storage_spec(tc, STABLE(type));
    if (ss->boxed_primitive) {
        return ss->boxed_primitive == wanted_prim_spec;
    }
    if (ss->can_box) {
        if (wanted_prim_spec == MVM_STORAGE_SPEC_BP_INT) {
            return ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_INT;
        }
        else if (wanted_prim_spec == MVM_STORAGE_SPEC_BP_NUM) {
            return ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_NUM;
        }
        else if (wanted_prim_spec == MVM_STORAGE_SPEC_BP_STR) {
            return ss->can_box & MVM_STORAGE_SPEC_CAN_BOX_STR;
        }
        else
            MVM_panic(1, "Spesh error: unexpected wanted_prim_spec %d", wanted_prim_spec);
    }
    return 0;
}

static void insert_getarg_and_box(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                  MVMSpeshIns *insert_point, MVMCallsiteFlags arg_flags,
                                  MVMint32 argument_idx, MVMSpeshOperand value_temp) {
    /* Instruction to get value depends on argument type. */
    if ((arg_flags & MVM_CALLSITE_ARG_TYPE_MASK) == MVM_CALLSITE_ARG_OBJ) {
        /* It's already a boxed object, so just fetch it into the value
         * register. */
        MVMSpeshIns *fetch_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        fetch_ins->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
        fetch_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
        fetch_ins->operands[0] = value_temp;
        fetch_ins->operands[1].lit_ui16 = argument_idx;
        MVM_spesh_manipulate_insert_ins(tc, bb, insert_point, fetch_ins);
    }
    else {
        MVMSpeshIns *box_ins, *hlltype_ins, *fetch_ins;

        /* We need to box it. Get a temporary register to box into, and one
         * for the HLL type. */
        MVMSpeshOperand unboxed_temp, box_type_temp;
        MVMuint16 box_op;
        MVMuint16 hlltype_op;
        MVMuint16 fetch_op;
        switch (arg_flags & MVM_CALLSITE_ARG_TYPE_MASK) {
            case MVM_CALLSITE_ARG_INT:
                unboxed_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
                box_op = MVM_OP_box_i;
                hlltype_op = MVM_OP_hllboxtype_i;
                fetch_op = MVM_OP_sp_getarg_i;
                break;
            case MVM_CALLSITE_ARG_NUM:
                unboxed_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_num64);
                box_op = MVM_OP_box_n;
                hlltype_op = MVM_OP_hllboxtype_n;
                fetch_op = MVM_OP_sp_getarg_n;
                break;
            case MVM_CALLSITE_ARG_STR:
                unboxed_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
                box_op = MVM_OP_box_s;
                hlltype_op = MVM_OP_hllboxtype_s;
                fetch_op = MVM_OP_sp_getarg_s;
                break;
            default:
                MVM_panic(1, "Spesh args: unexpected named argument type %d", arg_flags);
        }

        /* Emit instruction to box value. */
        box_type_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);
        box_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        box_ins->info = MVM_op_get_op(box_op);
        box_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
        box_ins->operands[0] = value_temp;
        box_ins->operands[1] = unboxed_temp;
        box_ins->operands[2] = box_type_temp;
        MVM_spesh_manipulate_insert_ins(tc, bb, insert_point, box_ins);
        MVM_spesh_get_facts(tc, g, value_temp)->writer = box_ins;

        /* Prepend the instruction get box type. */
        hlltype_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        hlltype_ins->info = MVM_op_get_op(hlltype_op);
        hlltype_ins->operands = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
        hlltype_ins->operands[0] = box_type_temp;
        MVM_spesh_manipulate_insert_ins(tc, bb, insert_point, hlltype_ins);
        MVM_spesh_get_facts(tc, g, box_type_temp)->writer = hlltype_ins;

        /* Prepend fetch instruction. */
        fetch_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        fetch_ins->info = MVM_op_get_op(fetch_op);
        fetch_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
        fetch_ins->operands[0] = unboxed_temp;
        fetch_ins->operands[1].lit_ui16 = argument_idx;
        MVM_spesh_manipulate_insert_ins(tc, bb, insert_point, fetch_ins);
        MVM_spesh_get_facts(tc, g, unboxed_temp)->writer = fetch_ins;

        /* Can release the temporary register now. */
        MVM_spesh_manipulate_release_temp_reg(tc, g, unboxed_temp);
        MVM_spesh_manipulate_release_temp_reg(tc, g, box_type_temp);
    }
}

/* Puts a single named argument into a slurpy hash, boxing if needed. */
static void slurp_named_arg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                            MVMSpeshIns *hash_ins, MVMint32 named_idx) {
    MVMSpeshIns *key_ins;

    /* Look up arg flags and name, and compute index. */
    MVMCallsiteFlags flags = g->cs->arg_flags[g->cs->num_pos + named_idx];
    MVMString *name = g->cs->arg_names[named_idx];
    MVMuint16 arg_idx = g->cs->num_pos + 2 * named_idx + 1;

    /* Allocate temporary registers for the key and value. */
    MVMSpeshOperand key_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_str);
    MVMSpeshOperand value_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);

    /* Insert bind key instruction after slurpy hash creation instruction (we
     * do it first as below we prepend instructions to obtain the key and the
     * value. */
    MVMSpeshIns *bindkey_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    bindkey_ins->info = MVM_op_get_op(MVM_OP_bindkey_o);
    bindkey_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    bindkey_ins->operands[0] = hash_ins->operands[0];
    bindkey_ins->operands[1] = key_temp;
    bindkey_ins->operands[2] = value_temp;
    MVM_spesh_manipulate_insert_ins(tc, bb, hash_ins, bindkey_ins);

    insert_getarg_and_box(tc, g, bb, hash_ins, flags, arg_idx, value_temp);

    /* Insert key fetching instruciton; we just store the string in a spesh
     * slot. */
    key_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    key_ins->info = MVM_op_get_op(MVM_OP_sp_getspeshslot);
    key_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    key_ins->operands[0] = key_temp;
    key_ins->operands[1].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)name);
    MVM_spesh_manipulate_insert_ins(tc, bb, hash_ins, key_ins);

    /* Release temporary registers after. */
    MVM_spesh_manipulate_release_temp_reg(tc, g, key_temp);
    MVM_spesh_manipulate_release_temp_reg(tc, g, value_temp);
}

static void slurp_positional_arg(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                                 MVMSpeshIns *array_ins, MVMint32 pos_idx, MVMint32 target_idx) {
    MVMSpeshIns *index_ins;

    /* Look up arg flags and name, and compute index. */
    MVMCallsiteFlags flags = g->cs->arg_flags[pos_idx];

    /* Allocate temporary registers for the key and value. */
    MVMSpeshOperand index_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_int64);
    MVMSpeshOperand value_temp = MVM_spesh_manipulate_get_temp_reg(tc, g, MVM_reg_obj);

    /* Insert bind key instruction after slurpy hash creation instruction (we
     * do it first as below we prepend instructions to obtain the key and the
     * value. */
    MVMSpeshIns *bindpos_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    bindpos_ins->info = MVM_op_get_op(MVM_OP_bindpos_o);
    bindpos_ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
    bindpos_ins->operands[0] = array_ins->operands[0];
    bindpos_ins->operands[1] = index_temp;
    bindpos_ins->operands[2] = value_temp;
    MVM_spesh_manipulate_insert_ins(tc, bb, array_ins, bindpos_ins);

    insert_getarg_and_box(tc, g, bb, array_ins, flags, pos_idx, value_temp);

    /* Insert index setting instruciton */
    index_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    index_ins->info = MVM_op_get_op(MVM_OP_const_i64_16);
    index_ins->operands = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    index_ins->operands[0] = index_temp;
    index_ins->operands[1].lit_i16 = target_idx;
    MVM_spesh_manipulate_insert_ins(tc, bb, array_ins, index_ins);

    /* Release temporary registers after. */
    MVM_spesh_manipulate_release_temp_reg(tc, g, index_temp);
    MVM_spesh_manipulate_release_temp_reg(tc, g, value_temp);
}

/* Takes information about the incoming callsite and arguments, and performs
 * various optimizations based on that information. */
void MVM_spesh_args(MVMThreadContext *tc, MVMSpeshGraph *g, MVMCallsite *cs,
                    MVMSpeshStatsType *type_tuple) {
    /* We need to identify the various arg-related instructions in the graph,
     * then manipulate them as a whole. */
    MVMSpeshIns  *checkarity_ins     = NULL;
    MVMSpeshBB   *checkarity_bb      = NULL;
    MVMSpeshIns  *paramnamesused_ins = NULL;
    MVMSpeshBB   *paramnamesused_bb  = NULL;
    MVMSpeshIns  *param_sn_ins       = NULL;
    MVMSpeshBB   *param_sn_bb        = NULL;
    MVMSpeshIns  *param_sp_ins       = NULL;
    MVMSpeshBB   *param_sp_bb        = NULL;

    MVMSpeshIns **pos_ins    = MVM_calloc(MAX_POS_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **pos_bb     = MVM_calloc(MAX_POS_ARGS, sizeof(MVMSpeshBB *));
    MVMuint8     *pos_added  = MVM_calloc(MAX_POS_ARGS, sizeof(MVMuint8));
    MVMSpeshIns **named_ins  = MVM_calloc(MAX_NAMED_ARGS, sizeof(MVMSpeshIns *));
    MVMSpeshBB  **named_bb   = MVM_calloc(MAX_NAMED_ARGS, sizeof(MVMSpeshBB *));
    MVMint32      req_max    = -1;
    MVMint32      opt_min    = -1;
    MVMint32      opt_max    = -1;
    MVMint32      num_named  = 0;
    MVMint32      named_used = 0;
    MVMint32      named_passed = (cs->arg_count - cs->num_pos) / 2;
    MVMint32      cs_flags   = cs->num_pos + named_passed;
    MVMint32      cur_ins = 0;

    /* We use a bit field to track named argument use; on deopt we will put it
     * into the deoptimized frame. */
    MVMuint64 named_used_bit_field = 0;

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
            case MVM_OP_param_rp_u:
            case MVM_OP_param_rp_n:
            case MVM_OP_param_rp_s:
            case MVM_OP_param_rp_o: {
                /* Required positional. */
                MVMint16 idx = ins->operands[1].lit_i16;
                if (idx < 0 || idx >= MAX_POS_ARGS)
                    goto cleanup;
                if (pos_ins[idx]) { /* Dupe; weird. */
                    MVM_spesh_graph_add_comment(tc, g, ins,
                            "bailed argument spesh: duplicate access to argument %ld", idx);
                    goto cleanup;
                }
                pos_ins[idx] = ins;
                pos_bb[idx]  = bb;
                if (idx > req_max)
                    req_max = idx;
                break;
            }
            case MVM_OP_param_op_i:
            case MVM_OP_param_op_u:
            case MVM_OP_param_op_n:
            case MVM_OP_param_op_s:
            case MVM_OP_param_op_o: {
                /* Optional Positional int/num/string/object */
                MVMint16 idx = ins->operands[1].lit_i16;
                if (idx < 0 || idx >= MAX_POS_ARGS)
                    goto cleanup;
                if (pos_ins[idx]) { /* Dupe; weird. */
                    MVM_spesh_graph_add_comment(tc, g, ins,
                            "bailed argument spesh: duplicate access to argument %ld", idx);
                    goto cleanup;
                }
                pos_ins[idx] = ins;
                pos_bb[idx]  = bb;
                if (idx > opt_max)
                    opt_max = idx;
                if (opt_min == -1 || idx < opt_min)
                    opt_min = idx;
                break;
            }
            case MVM_OP_param_on_i:
            case MVM_OP_param_on_u:
            case MVM_OP_param_on_n:
            case MVM_OP_param_on_s:
            case MVM_OP_param_on_o:
            case MVM_OP_param_rn_i:
            case MVM_OP_param_rn_u:
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
                param_sp_ins = ins;
                param_sp_bb = bb;
                break;
            case MVM_OP_param_sn:
                param_sn_ins = ins;
                param_sn_bb = bb;
                break;
            case MVM_OP_usecapture:
            case MVM_OP_savecapture:
                /* Require full args processing context for now; bail. */
                MVM_spesh_graph_add_comment(tc, g, ins,
                        "bailed argument spesh: support for %s NYI", ins->info->name);
                goto cleanup;
            case MVM_OP_paramnamesused:
                if (paramnamesused_ins)
                    goto cleanup; /* Dupe; weird; bail out! */
                paramnamesused_ins = ins;
                paramnamesused_bb  = bb;
                break;
            case MVM_OP_param_rn2_i:
            case MVM_OP_param_rn2_n:
            case MVM_OP_param_rn2_s:
            case MVM_OP_param_rn2_o:
            case MVM_OP_param_on2_i:
            case MVM_OP_param_on2_n:
            case MVM_OP_param_on2_s:
            case MVM_OP_param_on2_o:
            case MVM_OP_param_rn2_u:
            case MVM_OP_param_on2_u:
                /* Don't understand how to specialize these yet. */
                MVM_spesh_graph_add_comment(tc, g, ins,
                        "bailed argument spesh: support for %s NYI", ins->info->name);
                goto cleanup;
            default:
                break;
            }
            cur_ins++;
            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* If we didn't find a checkarity instruction, bail. */
    if (!checkarity_ins) {
        MVM_spesh_graph_add_comment(tc, g, g->entry->first_ins,
                "bailed argument spesh: no checkarity instruction found");
        goto cleanup;
    }

    /* If required and optional aren't contiguous, bail. */
    if (opt_min >= 0 && req_max + 1 != opt_min) {
        MVM_spesh_graph_add_comment(tc, g, g->entry->first_ins,
                "bailed argument spesh: required and optional args aren't contiguous");
        goto cleanup;
    }

    /* If the number of passed args is in range... */
    if (cs->num_pos >= req_max + 1 && (opt_max < 0 || cs->num_pos <= opt_max + 1)) {
        /* Ensure we've got all the arg fetch instructions we need, and that
         * types match or it's a box/unbox. */
        MVMint32 i;
        for (i = 0; i < cs->num_pos; i++) {
            MVMCallsiteEntry arg_flag = cs->arg_flags[i];
            if (pos_ins[i]) {
                switch (pos_ins[i]->info->opcode) {
                case MVM_OP_param_rp_i:
                case MVM_OP_param_op_i:
                    if (arg_flag != MVM_CALLSITE_ARG_INT)
                        if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                                !cmp_prim_spec(tc, type_tuple, i, MVM_STORAGE_SPEC_BP_INT)) {
                            MVM_spesh_graph_add_comment(tc, g, pos_ins[i],
                                    "bailed argument spesh: expected arg flag %ld to be int or box an int; type at position was %s", i, type_tuple && type_tuple[i].type ? MVM_6model_get_debug_name(tc, type_tuple[i].type) : "null type tuple");
                            goto cleanup;
                        }
                    break;
                case MVM_OP_param_rp_u:
                case MVM_OP_param_op_u:
                    if (arg_flag != MVM_CALLSITE_ARG_INT)
                        if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                                !cmp_prim_spec(tc, type_tuple, i, MVM_STORAGE_SPEC_BP_INT)) {
                            MVM_spesh_graph_add_comment(tc, g, pos_ins[i],
                                    "bailed argument spesh: expected arg flag %ld to be int or box an int; type at position was %s", i, type_tuple && type_tuple[i].type ? MVM_6model_get_debug_name(tc, type_tuple[i].type) : "null type tuple");
                            goto cleanup;
                        }
                    break;
                case MVM_OP_param_rp_n:
                case MVM_OP_param_op_n:
                    if (arg_flag != MVM_CALLSITE_ARG_NUM)
                        if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                                !cmp_prim_spec(tc, type_tuple, i, MVM_STORAGE_SPEC_BP_NUM)) {
                            MVM_spesh_graph_add_comment(tc, g, pos_ins[i],
                                    "bailed argument spesh: expected arg flag %ld to be num or box a num", i);
                            goto cleanup;
                        }
                    break;
                case MVM_OP_param_rp_s:
                case MVM_OP_param_op_s:
                    if (arg_flag != MVM_CALLSITE_ARG_STR)
                        if (arg_flag != MVM_CALLSITE_ARG_OBJ ||
                                !cmp_prim_spec(tc, type_tuple, i, MVM_STORAGE_SPEC_BP_STR)) {
                            MVM_spesh_graph_add_comment(tc, g, pos_ins[i],
                                    "bailed argument spesh: expected arg flag %ld to be str or box a str type at position was %s", i, type_tuple && type_tuple[i].type ? MVM_6model_get_debug_name(tc, type_tuple[i].type) : "null type tuple");
                            goto cleanup;
                        }
                    break;
                case MVM_OP_param_rp_o:
                case MVM_OP_param_op_o:
                    if (arg_flag != MVM_CALLSITE_ARG_OBJ && arg_flag != MVM_CALLSITE_ARG_INT &&
                        arg_flag != MVM_CALLSITE_ARG_NUM && arg_flag != MVM_CALLSITE_ARG_STR) {
                        MVM_spesh_graph_add_comment(tc, g, pos_ins[i],
                                "bailed argument spesh: expected arg flag %ld to be obj or int or num or str", i);
                        goto cleanup;
                    }
                    break;
                default:
                    break;
                }
            }
            else if (!pos_ins[i] && !param_sp_ins) {
                MVM_spesh_graph_add_comment(tc, g, g->entry->first_ins,
                        "bailed argument spesh: no positional arg fetch op found for %ld", i);
                goto cleanup;
            }
        }

        /* Check the argument types of named arguments is something we know how
         * to handle. */
        for (i = 0; i < num_named; i++) {
            /* Get the flag of the passed arg, if any. */
            MVMString *arg_name       = MVM_spesh_get_string(tc, g, named_ins[i]->operands[1]);
            MVMint32   cur_idx        = 0;
            MVMint32   cur_named      = 0;
            MVMuint8   found_flag     = 0;
            MVMint32   found_flag_idx = -1;
            MVMint32   found_idx      = -1;
            MVMint32   j;
            for (j = 0; j < cs_flags; j++) {
                if (cs->arg_flags[j] & MVM_CALLSITE_ARG_NAMED) {
                    if (MVM_string_equal(tc, arg_name, cs->arg_names[cur_named])) {
                        /* Found it. */
                        found_flag_idx = j;
                        found_flag = cs->arg_flags[j] & MVM_CALLSITE_ARG_TYPE_MASK;
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

            /* Now go by op. */
            switch (named_ins[i]->info->opcode) {
            case MVM_OP_param_rn_i:
                if (found_idx == -1) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: required named argument not found!");
                    goto cleanup;
                }
                if (!(found_flag & MVM_CALLSITE_ARG_INT)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_INT)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_rn_n:
                if (found_idx == -1) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: required named argument not found!");
                    goto cleanup;
                }
                if (!(found_flag & MVM_CALLSITE_ARG_NUM)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_NUM)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_rn_s:
                if (found_idx == -1) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: required named argument not found!");
                    goto cleanup;
                }
                if (!(found_flag & MVM_CALLSITE_ARG_STR)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_STR)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_rn_o:
                if (found_idx == -1) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: required named argument not found!");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_on_i:
                if (found_idx != -1 && !(found_flag & MVM_CALLSITE_ARG_INT)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_INT)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_on_n:
                if (found_idx != -1 && !(found_flag & MVM_CALLSITE_ARG_NUM)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_NUM)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            case MVM_OP_param_on_s:
                if (found_idx != -1 && !(found_flag & MVM_CALLSITE_ARG_STR)
                        && !(found_flag & MVM_CALLSITE_ARG_OBJ
                            && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_STR)) {
                    MVM_spesh_graph_add_comment(tc, g, named_ins[i],
                            "bailed argument spesh: unhandled coercion");
                    goto cleanup;
                }
                break;
            default:
                break;
            }
        }

        /* We can optimize. Toss checkarity. */
        MVM_spesh_manipulate_delete_ins(tc, g, checkarity_bb, checkarity_ins);

        /* Re-write the passed required positionals to spesh ops, and add any
         * facts. */
        for (i = 0; i < cs->num_pos; i++) {
            MVMCallsiteEntry arg_flag = cs->arg_flags[i];
            if (!pos_ins[i])
                continue;
            switch (pos_ins[i]->info->opcode) {
                /* FIXME add support for param_rp_u here */
            case MVM_OP_param_rp_i:
            case MVM_OP_param_op_i:
                if (arg_flag == MVM_CALLSITE_ARG_INT) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_decont_i));
                    pos_added[i]++;
                    if (type_tuple && type_tuple[i].type)
                        add_facts(tc, g, i, type_tuple[i], pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_u:
            case MVM_OP_param_op_u:
                if (arg_flag == MVM_CALLSITE_ARG_INT) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_decont_i));
                    pos_added[i]++;
                    if (type_tuple && type_tuple[i].type)
                        add_facts(tc, g, i, type_tuple[i], pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_n:
            case MVM_OP_param_op_n:
                if (arg_flag == MVM_CALLSITE_ARG_NUM) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_decont_n));
                    pos_added[i]++;
                    if (type_tuple && type_tuple[i].type)
                        add_facts(tc, g, i, type_tuple[i], pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_s:
            case MVM_OP_param_op_s:
                if (arg_flag == MVM_CALLSITE_ARG_STR) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                }
                else {
                    pos_unbox(tc, g, pos_bb[i], pos_ins[i], MVM_op_get_op(MVM_OP_decont_s));
                    pos_added[i]++;
                    if (type_tuple && type_tuple[i].type)
                        add_facts(tc, g, i, type_tuple[i], pos_ins[i]);
                }
                break;
            case MVM_OP_param_rp_o:
            case MVM_OP_param_op_o:
                if (arg_flag == MVM_CALLSITE_ARG_OBJ) {
                    pos_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    if (type_tuple && type_tuple[i].type) {
                        add_facts(tc, g, i, type_tuple[i], pos_ins[i]);
                        if (i == 0)
                            g->specialized_on_invocant = 1;
                    }
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
            default:
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
            MVMString *arg_name       = MVM_spesh_get_string(tc, g, named_ins[i]->operands[1]);
            MVMint32   cur_idx        = 0;
            MVMint32   cur_named      = 0;
            MVMuint8   found_flag     = 0;
            MVMint32   found_idx      = -1;
            MVMint32   found_flag_idx = -1;
            MVMint32   j;
            for (j = 0; j < cs_flags; j++) {
                if (cs->arg_flags[j] & MVM_CALLSITE_ARG_NAMED) {
                    if (MVM_string_equal(tc, arg_name, cs->arg_names[cur_named])) {
                        /* Found it. */
                        found_flag_idx = j;
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
                if (found_flag & MVM_CALLSITE_ARG_INT) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_i);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_INT) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_i));
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                named_used++;
                break;
            case MVM_OP_param_rn_n:
                if (found_flag & MVM_CALLSITE_ARG_NUM) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_n);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_NUM) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_n));
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                named_used++;
                break;
            case MVM_OP_param_rn_s:
                if (found_flag & MVM_CALLSITE_ARG_STR) {
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_s);
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_STR) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_s));
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                }
                named_used++;
                break;
            case MVM_OP_param_rn_o:
                if (found_flag & MVM_CALLSITE_ARG_OBJ) {
                    MVMuint16 arg_idx = found_idx + 1;
                    named_ins[i]->info = MVM_op_get_op(MVM_OP_sp_getarg_o);
                    named_ins[i]->operands[1].lit_i16 = arg_idx;
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    if (type_tuple && type_tuple[found_flag_idx].type)
                        add_facts(tc, g, arg_idx, type_tuple[found_flag_idx], named_ins[i]);
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
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
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
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_INT) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_i));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
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
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_NUM) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_n));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
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
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    named_used++;
                }
                else if (found_flag & MVM_CALLSITE_ARG_OBJ
                        && prim_spec(tc, type_tuple, found_flag_idx) == MVM_STORAGE_SPEC_BP_STR) {
                    named_ins[i]->operands[1].lit_i16 = found_idx + 1;
                    pos_unbox(tc, g, named_bb[i], named_ins[i], MVM_op_get_op(MVM_OP_decont_s));
                    MVM_spesh_manipulate_insert_goto(tc, g, named_bb[i], named_ins[i]->next,
                        named_ins[i]->operands[2].ins_bb);
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
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
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    if (type_tuple && type_tuple[found_flag_idx].type)
                        add_facts(tc, g, arg_idx, type_tuple[found_flag_idx], named_ins[i]);
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
                    MVM_spesh_manipulate_remove_successor(tc, named_bb[i],
                        named_bb[i]->linear_next);
                    named_used_bit_field |= (MVMuint64)1 << cur_named;
                    named_used++;
                }
                break;
            default:
                break;
            }
        }

        /* If we have an instruction to check all nameds were used... */
        if (paramnamesused_ins) {
            /* Delete it if they were. */
            if (named_passed == named_used) {
                MVM_spesh_manipulate_delete_ins(tc, g, paramnamesused_bb, paramnamesused_ins);
            }

            /* Otherwise, we have unexpected named arguments. Turn it into an
             * error. */
            else {
                MVMuint16 i;
                for (i = 0; i < named_passed; i++) {
                    if (!(named_used_bit_field & ((MVMuint64)1 << i))) {
                        paramnamesused_ins->info = MVM_op_get_op(MVM_OP_sp_paramnamesused);
                        paramnamesused_ins->operands = MVM_spesh_alloc(tc,
                            g, sizeof(MVMSpeshOperand));
                        paramnamesused_ins->operands[0].lit_i16 = MVM_spesh_add_spesh_slot(tc,
                            g, (MVMCollectable *)g->cs->arg_names[i]);
                        break;
                    }
                }
            }
        }

        /* If we have a slurpy hash... */
        if (param_sn_ins) {
            /* Construct it as a hash. */
            MVMObject *hash_type = g->sf->body.cu->body.hll_config->slurpy_hash_type;
            if (REPR(hash_type)->ID == MVM_REPR_ID_MVMHash && !REPR(hash_type)->initialize) {
                MVMSpeshOperand target    = param_sn_ins->operands[0];
                param_sn_ins->info        = MVM_op_get_op(MVM_OP_sp_fastcreate);
                param_sn_ins->operands    = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                param_sn_ins->operands[0] = target;
                param_sn_ins->operands[1].lit_i16 = sizeof(MVMHash);
                param_sn_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g,
                    (MVMCollectable *)STABLE(hash_type));
                MVM_spesh_graph_add_comment(tc, g, param_sn_ins, "argument spesh: slurpy nameds optimized");
            }
            else {
                MVM_oops(tc, "Arg spesh: slurpy hash type was not a VMHash as expected");
            }

            /* Populate it with unused named args, if needed, boxing them on
             * the way. */
            if (named_passed > named_used)
                for (i = 0; i < named_passed; i++)
                    if (!(named_used_bit_field & ((MVMuint64)1 << i)))
                        slurp_named_arg(tc, g, param_sn_bb, param_sn_ins, i);
        }

        /* If we have a slurpy array ... */
        if (param_sp_ins) {
            MVMint32 start_slurping_at = param_sp_ins->operands[1].lit_i16;
            /* Construct it as a hash. */
            MVMObject *array_type = g->sf->body.cu->body.hll_config->slurpy_array_type;
            if (REPR(array_type)->ID == MVM_REPR_ID_VMArray && !REPR(array_type)->initialize) {
                MVMSpeshOperand target    = param_sp_ins->operands[0];
                param_sp_ins->info        = MVM_op_get_op(MVM_OP_sp_fastcreate);
                param_sp_ins->operands    = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
                param_sp_ins->operands[0] = target;
                param_sp_ins->operands[1].lit_i16 = sizeof(MVMArray);
                param_sp_ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g,
                    (MVMCollectable *)STABLE(array_type));
                MVM_spesh_graph_add_comment(tc, g, param_sp_ins, "argument spesh: slurpy positional optimized");
            }
            else {
                MVM_oops(tc, "Arg spesh: slurpy array type was not a VMArray as expected");
            }

            /* Populate it with all args after the numeral argument */
            for (i = cs->num_pos - 1; i >= start_slurping_at; i--)
                slurp_positional_arg(tc, g, param_sp_bb, param_sp_ins, i, i - start_slurping_at);
        }

        /* Stash the named used bit field in the graph; will need to make it
         * into the candidate and all the way to deopt. */
        g->deopt_named_used_bit_field = named_used_bit_field;
    }

  cleanup:
    MVM_free(pos_ins);
    MVM_free(pos_bb);
    MVM_free(pos_added);
    MVM_free(named_ins);
    MVM_free(named_bb);
}

/* Performs argument instruction specialization with type info provided by a
 * call_info object. */
void MVM_spesh_args_from_callinfo(MVMThreadContext *tc, MVMSpeshGraph *g,
                                  MVMSpeshCallInfo *call_info,
                                  MVMSpeshStatsType *type_tuple) {
    /* If we have a type tuple passed, just use that. */
    if (type_tuple) {
        MVM_spesh_args(tc, g, call_info->cs, type_tuple);
    }

    /* Otherwise, transform call info to a type tuple, and use that. */
    else {
        MVMuint16 i;
        MVMuint16 flags = call_info->cs->flag_count;
        MVMSpeshStatsType *tt = MVM_calloc(flags, sizeof(MVMSpeshStatsType));
        MVMuint16 info_pos = 0;
        for (i = 0; i < flags; i++) {
            MVMCallsiteEntry flag = call_info->cs->arg_flags[i];
            if (flag & MVM_CALLSITE_ARG_NAMED)
                info_pos++; /* Skip over named. */
            if (info_pos >= MAX_ARGS_FOR_OPT) {
                MVM_free(tt);
                return;
            }
            if (flag & MVM_CALLSITE_ARG_OBJ) {
                MVMSpeshFacts *facts = call_info->arg_facts[info_pos];
                if (facts) {
                    if (facts->flags & MVM_SPESH_FACT_KNOWN_TYPE &&
                            (facts->flags & (MVM_SPESH_FACT_CONCRETE | MVM_SPESH_FACT_TYPEOBJ))) {
                        tt[i].type = facts->type;
                        tt[i].type_concrete = facts->flags & MVM_SPESH_FACT_CONCRETE;
                    }
                    else if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
                        tt[i].type = STABLE(facts->value.o)->WHAT;
                        tt[i].type_concrete = IS_CONCRETE(facts->value.o);
                    }
                }
            }
            info_pos++;
        }
        MVM_spesh_args(tc, g, call_info->cs, tt);
        MVM_free(tt);
    }
}
