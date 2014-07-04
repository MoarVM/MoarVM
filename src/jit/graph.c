#include "moar.h"

static void append_ins(MVMJitGraph *jg, MVMJitIns *ins) {
    if (jg->last_ins) {
        jg->last_ins->next = ins;
        jg->last_ins = ins;
    } else {
        jg->first_ins = ins;
        jg->last_ins = ins;
    }
    ins->next = NULL;
}

static void append_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                             MVMSpeshIns * moar_ins) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_PRIMITIVE;
    ins->u.prim.ins = moar_ins;
    append_ins(jg, ins);
}

static void append_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                          void * func_ptr, MVMint16 num_args,
                          MVMJitAddr *call_args, MVMJitRVMode rv_mode,
                          MVMint16 rv_idx) {
    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    size_t args_size =  num_args * sizeof(MVMJitAddr);
    ins->type             = MVM_JIT_INS_CALL_C;
    ins->u.call.func_ptr  = func_ptr;
    ins->u.call.num_args  = num_args;
    ins->u.call.has_vargs = 0; // don't support them yet
    /* Call argument array is typically stack allocated,
     * so they need to be copied */
    ins->u.call.args      = MVM_spesh_alloc(tc, jg->spesh, args_size);
    memcpy(ins->u.call.args, call_args, args_size);
    ins->u.call.rv_mode   = rv_mode;
    ins->u.call.rv_idx    = rv_idx;
    append_ins(jg, ins);
}


/* Try to assign a label name for a basic block */
static MVMint32 get_label_name(MVMThreadContext *tc, MVMJitGraph *jg,
                               MVMSpeshBB *bb) {
    int i = 0;
    for (i = 0; i < jg->num_labels; i++) {
        if (jg->labels[i].bb == bb) {
            return i;
        } else if (jg->labels[i].bb == NULL) {
            jg->labels[i].bb = bb;
            return i;
        }
    }
    MVM_exception_throw_adhoc(tc, "JIT: Cannot assign %d labels", i);
}

static void append_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMint32 name, MVMSpeshIns *sp_ins) {

    MVMJitIns * ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_BRANCH;
    if (sp_ins == NULL) {
        ins->u.branch.ins = NULL;
        ins->u.branch.dest.bb = NULL;
        ins->u.branch.dest.name = name;
    }
    else {
        ins->u.branch.ins = sp_ins;
        if (sp_ins->info->opcode == MVM_OP_goto) {
            ins->u.branch.dest.bb = sp_ins->operands[0].ins_bb;
        }
        else {
            ins->u.branch.dest.bb = sp_ins->operands[1].ins_bb;
        }
        ins->u.branch.dest.name = get_label_name(tc, jg, ins->u.branch.dest.bb);
    }
    append_ins(jg, ins);
}

static void append_label(MVMThreadContext *tc, MVMJitGraph *jg,
                         MVMSpeshBB *bb) {

    MVMJitIns *ins = MVM_spesh_alloc(tc, jg->spesh, sizeof(MVMJitIns));
    ins->type = MVM_JIT_INS_LABEL;
    ins->u.label.bb = bb;
    ins->u.label.name = get_label_name(tc, jg, bb);
    append_ins(jg, ins);
    MVM_jit_log(tc, "append label: %d\n", ins->u.label.name);
}

static void * op_to_func(MVMThreadContext *tc, MVMint16 opcode) {
    switch(opcode) {
    case MVM_OP_say: return &MVM_string_say;
    case MVM_OP_print: return &MVM_string_print;
    case MVM_OP_return: return &MVM_args_assert_void_return_ok;
    case MVM_OP_return_i: return &MVM_args_set_result_int;
    case MVM_OP_return_s: return &MVM_args_set_result_str;
    case MVM_OP_return_o: return &MVM_args_set_result_obj;
    case MVM_OP_return_n: return &MVM_args_set_result_num;
    case MVM_OP_coerce_is: return &MVM_coerce_i_s;
    case MVM_OP_coerce_ns: return &MVM_coerce_n_s;
    case MVM_OP_coerce_si: return &MVM_coerce_s_i;
    case MVM_OP_coerce_sn: return &MVM_coerce_s_n;
    case MVM_OP_wval: case MVM_OP_wval_wide: return &MVM_sc_get_sc_object;
    default:
        MVM_exception_throw_adhoc(tc, "No function for op %d", opcode);
    }
}

static void append_guard(MVMThreadContext *tc, MVMJitGraph *jg, 
                         MVMSpeshIns *ins) {
    MVMSpeshGraph *sg = jg->spesh;
    MVMSpeshAnn  *ann = ins->annotations;
    MVMJitIns     *ji = MVM_spesh_alloc(tc, sg, sizeof(MVMJitIns));
    MVMint32       deopt_idx;
    ji->type = MVM_JIT_INS_GUARD;
    ji->u.guard.ins = ins;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS ||
            ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS || 
            ann->type == MVM_SPESH_ANN_DEOPT_INLINE  ||
            ann->type == MVM_SPESH_ANN_DEOPT_OSR) {
            deopt_idx = ann->data.deopt_idx;
            break;
        }
        ann = ann->next;
    }
    if (!ann) {
        MVM_exception_throw_adhoc(tc, "Can't find deopt idx annotation" 
                                  " on spesh ins <%s>", ins->info->name);
    }
    ji->u.guard.deopt_target = sg->deopt_addrs[2 * deopt_idx];
    ji->u.guard.deopt_offset = sg->deopt_addrs[2 * deopt_idx + 1];
    append_ins(jg, ji);
}

static MVMint32 append_op(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMSpeshIns *ins) {
    int op = ins->info->opcode;
    MVM_jit_log(tc, "append_ins: <%s>\n", ins->info->name);
    switch(op) {
    case MVM_SSA_PHI:
    case MVM_OP_no_op:
        break;
        /* arithmetic */
    case MVM_OP_add_i:
    case MVM_OP_sub_i:
    case MVM_OP_mul_i:
    case MVM_OP_div_i:
    case MVM_OP_mod_i:
    case MVM_OP_inc_i:
    case MVM_OP_dec_i:
    case MVM_OP_add_n:
    case MVM_OP_sub_n:
    case MVM_OP_mul_n:
    case MVM_OP_div_n:
    case MVM_OP_coerce_in:
        /* comparison (integer) */
    case MVM_OP_eqaddr:
    case MVM_OP_eq_i:
    case MVM_OP_ne_i:
    case MVM_OP_lt_i:
    case MVM_OP_le_i:
    case MVM_OP_gt_i:
    case MVM_OP_ge_i:
        /* constants */
    case MVM_OP_const_i64_16:
    case MVM_OP_const_i64:
    case MVM_OP_const_n64:
    case MVM_OP_const_s:
    case MVM_OP_null:
        /* argument reading */
    case MVM_OP_sp_getarg_i:
    case MVM_OP_sp_getarg_o:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
        /* accessors */
    case MVM_OP_sp_p6oget_o:
    case MVM_OP_sp_p6oget_s:
    case MVM_OP_sp_p6oget_i:
    case MVM_OP_sp_p6oget_n:
        /*
    case MVM_OP_sp_p6ogetvc_o:
        */
    case MVM_OP_sp_p6ogetvt_o:

    case MVM_OP_sp_p6obind_o:
    case MVM_OP_sp_p6obind_s:
    case MVM_OP_sp_p6obind_n:
    case MVM_OP_sp_p6obind_i:
    case MVM_OP_set:
    case MVM_OP_getlex:
    case MVM_OP_bindlex:
    case MVM_OP_getwhat:
    case MVM_OP_gethow:
    case MVM_OP_getwhere:
        append_primitive(tc, jg, ins);
        break;
        /* branches */
    case MVM_OP_goto:
    case MVM_OP_if_i:
    case MVM_OP_unless_i:
        append_branch(tc, jg, 0, ins);
        break;
        /* some functions */
    case MVM_OP_say: 
    case MVM_OP_print: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_REG, reg } };
        append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_wval:
    case MVM_OP_wval_wide: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 dep = ins->operands[1].lit_i16;
        MVMint64 idx;
        if (op == MVM_OP_wval) {
            idx = ins->operands[2].lit_i16;
        } else {
            idx = ins->operands[2].lit_i64;
        }
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC },
                              { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_CU },
                              { MVM_JIT_ADDR_LITERAL, dep },
                              { MVM_JIT_ADDR_LITERAL, idx } };
        append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* coercion */
    case MVM_OP_coerce_sn: 
    case MVM_OP_coerce_ns: 
    case MVM_OP_coerce_si:
    case MVM_OP_coerce_is: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitAddr args[2] = {{ MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_REG, src } };
        MVMJitRVMode rv_mode = (op == MVM_OP_coerce_sn ? MVM_JIT_RV_NUM :
                                op == MVM_OP_coerce_si ? MVM_JIT_RV_INT :
                                MVM_JIT_RV_PTR);
        if (op == MVM_OP_coerce_ns) {
            args[1].base = MVM_JIT_ADDR_REG_F;
        } 
        append_call_c(tc, jg, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
        /* returning */
    case MVM_OP_return: {
        MVMJitAddr args[] = { { MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC},
                              { MVM_JIT_ADDR_LITERAL, 0 }};
        append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        append_branch(tc, jg, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_return_o:
    case MVM_OP_return_s:
    case MVM_OP_return_n:
    case MVM_OP_return_i: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitAddr args[3] = {{ MVM_JIT_ADDR_INTERP, MVM_JIT_INTERP_TC },
                              { MVM_JIT_ADDR_REG, reg },
                              { MVM_JIT_ADDR_LITERAL, 0 } };
        if (op == MVM_OP_return_n) {
            args[1].base == MVM_JIT_ADDR_REG_F;
        }
        append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        append_branch(tc, jg, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
        append_guard(tc, jg, ins);
        break;
    default:
        MVM_jit_log(tc, "Don't know how to make a graph of opcode <%s>\n",
                    ins->info->name);
        return 0;
    }
    return 1;
}

static MVMint32 append_bb(MVMThreadContext *tc, MVMJitGraph *jg,
                          MVMSpeshBB *bb) {
    append_label(tc, jg, bb);
    MVMSpeshIns *cur_ins = bb->first_ins;
    while (cur_ins) {
        if(!append_op(tc, jg, cur_ins))
            return 0;
        if (cur_ins == bb->last_ins)
            break;
        cur_ins = cur_ins->next;
    }
    return 1;
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    MVMSpeshBB *cur_bb;
    MVMJitGraph * jg;
    int i;
    if (!MVM_jit_support()) {
        return NULL;
    }

    MVM_jit_log(tc, "Constructing JIT graph\n");

    jg = MVM_spesh_alloc(tc, sg, sizeof(MVMJitGraph));
    jg->spesh = sg;
    jg->num_labels = sg->num_bbs;
    jg->labels = MVM_spesh_alloc(tc, sg, sizeof(MVMJitLabel) * jg->num_labels);

    /* ignore first (nop) BB, don't need it */
    cur_bb = sg->entry->linear_next;
    /* loop over basic blocks, adding one after the other */
    while (cur_bb) {
        if (!append_bb(tc, jg, cur_bb))
            return NULL;
        cur_bb = cur_bb->linear_next;
    }

    /* Check if we've added a instruction at all */
    if (jg->first_ins)
        return jg;
    return NULL;
}
