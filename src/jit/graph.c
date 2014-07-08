#include "moar.h"

typedef struct {
    MVMSpeshGraph    *sg;
    MVMSpeshBB   *cur_bb;
    MVMSpeshIns *cur_ins;

    MVMJitNode *first_node;
    MVMJitNode *last_node;

    MVMint32  num_labels;
    MVMJitLabel  *labels;
} JitGraphBuilder;


static void jgb_append_node(JitGraphBuilder *jgb, MVMJitNode *node) {
    if (jgb->last_node) {
        jgb->last_node->next = node;
        jgb->last_node = node;
    } else {
        jgb->first_node = node;
        jgb->last_node = node;
    }
    node->next = NULL;
}

static void jgb_append_primitive(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                 MVMSpeshIns * ins) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_PRIMITIVE;
    node->u.prim.ins = ins;
    jgb_append_node(jgb, node);
}

static void jgb_append_call_c(MVMThreadContext *tc, JitGraphBuilder *jgb,
                              void * func_ptr, MVMint16 num_args,
                              MVMJitCallArg *call_args,
                              MVMJitRVMode rv_mode, MVMint16 rv_idx) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    size_t args_size =  num_args * sizeof(MVMJitCallArg);
    node->type             = MVM_JIT_NODE_CALL_C;
    node->u.call.func_ptr  = func_ptr;
    node->u.call.num_args  = num_args;
    node->u.call.has_vargs = 0; // don't support them yet
    /* Call argument array is typically stack allocated,
     * so they need to be copied */
    node->u.call.args      = MVM_spesh_alloc(tc, jgb->sg, args_size);
    memcpy(node->u.call.args, call_args, args_size);
    node->u.call.rv_mode   = rv_mode;
    node->u.call.rv_idx    = rv_idx;
    jgb_append_node(jgb, node);
}


/* Try to assign a label name for a basic block */
static MVMint32 get_label_name(MVMThreadContext *tc, JitGraphBuilder *jgb,
                               MVMSpeshBB *bb) {
    int i = 0;
    for (i = 0; i < jgb->num_labels; i++) {
        if (jgb->labels[i].bb == bb) {
            return i;
        }
        else if (jgb->labels[i].bb == NULL) {
            jgb->labels[i].bb = bb;
            return i;
        }
    }
    MVM_exception_throw_adhoc(tc, "JIT: Cannot assign %d labels", i);
}

static void jgb_append_branch(MVMThreadContext *tc, JitGraphBuilder *jgb,
                              MVMint32 name, MVMSpeshIns *ins) {

    MVMJitNode * node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_BRANCH;
    if (ins == NULL) {
        node->u.branch.ins = NULL;
        node->u.branch.dest.bb = NULL;
        node->u.branch.dest.name = name;
    }
    else {
        node->u.branch.ins = ins;
        if (ins->info->opcode == MVM_OP_goto) {
            node->u.branch.dest.bb = ins->operands[0].ins_bb;
        }
        else {
            node->u.branch.dest.bb = ins->operands[1].ins_bb;
        }
        node->u.branch.dest.name = get_label_name(tc, jgb, node->u.branch.dest.bb);
    }
    jgb_append_node(jgb, node);
}

static void jgb_append_label(MVMThreadContext *tc, JitGraphBuilder *jgb,
                             MVMSpeshBB *bb) {

    MVMJitNode *node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_LABEL;
    node->u.label.bb = bb;
    node->u.label.name = get_label_name(tc, jgb, bb);
    jgb_append_node(jgb, node);
    MVM_jit_log(tc, "append label: %d\n", node->u.label.name);
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
    case MVM_OP_smrt_numify: return &MVM_coerce_smart_numify;
    case MVM_OP_wval: case MVM_OP_wval_wide: return &MVM_sc_get_sc_object;
    default:
        MVM_exception_throw_adhoc(tc, "No function for op %d", opcode);
    }
}

static void jgb_append_guard(MVMThreadContext *tc, JitGraphBuilder *jgb,
                             MVMSpeshIns *ins) {
    MVMSpeshAnn   *ann = ins->annotations;
    MVMJitNode   *node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    MVMint32 deopt_idx;
    node->type = MVM_JIT_NODE_GUARD;
    node->u.guard.ins = ins;
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
    node->u.guard.deopt_target = jgb->sg->deopt_addrs[2 * deopt_idx];
    node->u.guard.deopt_offset = jgb->sg->deopt_addrs[2 * deopt_idx + 1];
    jgb_append_node(jgb, node);
}

static MVMint32 jgb_consume_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
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
    case MVM_OP_sp_p6ogetvc_o:
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
    case MVM_OP_sp_getspeshslot:
        jgb_append_primitive(tc, jgb, ins);
        break;
        /* branches */
    case MVM_OP_goto:
    case MVM_OP_if_i:
    case MVM_OP_unless_i:
        jgb_append_branch(tc, jgb, 0, ins);
        break;
        /* some functions */
    case MVM_OP_say:
    case MVM_OP_print: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC},
                                 { MVM_JIT_REG_VAL, reg } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
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
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_CU },
                                 { MVM_JIT_LITERAL, dep },
                                 { MVM_JIT_LITERAL, idx } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* coercion */
    case MVM_OP_coerce_sn:
    case MVM_OP_coerce_ns:
    case MVM_OP_coerce_si:
    case MVM_OP_coerce_is: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[2] = {{ MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC},
                                 { MVM_JIT_REG_VAL, src } };
        MVMJitRVMode rv_mode = (op == MVM_OP_coerce_sn ? MVM_JIT_RV_NUM :
                                op == MVM_OP_coerce_si ? MVM_JIT_RV_INT :
                                MVM_JIT_RV_PTR);
        if (op == MVM_OP_coerce_ns) {
            args[1].type = MVM_JIT_REG_VAL_F;
        }
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
        /* returning */
    case MVM_OP_return: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC},
                                 { MVM_JIT_LITERAL, 0 }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        jgb_append_branch(tc, jgb, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_return_o:
    case MVM_OP_return_s:
    case MVM_OP_return_n:
    case MVM_OP_return_i: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[3] = {{ MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, reg },
                                 { MVM_JIT_LITERAL, 0 } };
        if (op == MVM_OP_return_n) {
            args[1].type == MVM_JIT_REG_VAL_F;
        }
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        jgb_append_branch(tc, jgb, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
        jgb_append_guard(tc, jgb, ins);
        break;
    default:
        MVM_jit_log(tc, "Don't know how to make a graph of opcode <%s>\n",
                    ins->info->name);
        return 0;
    }
    return 1;
}

static MVMint32 jgb_consume_bb(MVMThreadContext *tc, JitGraphBuilder *jgb,
                               MVMSpeshBB *bb) {
    jgb->cur_bb = bb;
    jgb_append_label(tc, jgb, bb);
    jgb->cur_ins = bb->first_ins;
    while (jgb->cur_ins) {
        if(!jgb_consume_ins(tc, jgb, jgb->cur_ins))
            return 0;
        if (jgb->cur_ins == bb->last_ins)
            break;
        jgb->cur_ins = jgb->cur_ins->next;
    }
    return 1;
}

static MVMJitGraph *jgb_build(MVMThreadContext *tc, JitGraphBuilder *jgb) {
    MVMJitGraph * jg = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitGraph));
    jg->sg         = jgb->sg;
    jg->num_labels = jgb->num_labels;
    jg->labels     = jgb->labels;
    jg->first_node = jgb->first_node;
    jg->last_node  = jgb->last_node;
    return jg;
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    JitGraphBuilder jgb;
    MVMJitGraph * jg;
    int i;
    if (!MVM_jit_support()) {
        return NULL;
    }

    MVM_jit_log(tc, "Constructing JIT graph\n");

    jgb.sg = sg;
    jgb.num_labels = sg->num_bbs;
    jgb.labels = MVM_spesh_alloc(tc, sg, sizeof(MVMJitLabel) * sg->num_bbs);
    /* ignore first BB, which always contains a NOP */
    jgb.cur_bb = sg->entry->linear_next;
    jgb.cur_ins = jgb.cur_bb->first_ins;
    jgb.first_node = jgb.last_node = NULL;
    /* loop over basic blocks, adding one after the other */
    while (jgb.cur_bb) {
        if (!jgb_consume_bb(tc, &jgb, jgb.cur_bb))
            return NULL;
        jgb.cur_bb = jgb.cur_bb->linear_next;
    }
    /* Check if we've added a instruction at all */
    if (jgb.first_node)
        return jgb_build(tc, &jgb);
    return NULL;
}
