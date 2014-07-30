#include "moar.h"

typedef struct {
    MVMSpeshGraph      *sg;
    MVMSpeshBB     *cur_bb;
    MVMSpeshIns   *cur_ins;

    MVMJitNode *first_node;
    MVMJitNode  *last_node;

    MVMint32    num_labels;
    MVMJitLabel    *labels;

    MVMint32  *osr_offsets;
    MVMint32   num_offsets;
    MVMint32 alloc_offsets;
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
    case MVM_OP_checkarity: return &MVM_args_checkarity;
    case MVM_OP_say: return &MVM_string_say;
    case MVM_OP_print: return &MVM_string_print;
    case MVM_OP_isnull: return &MVM_is_null;
    case MVM_OP_takeclosure: return &MVM_frame_takeclosure;
    case MVM_OP_newlexotic: return &MVM_exception_newlexotic;
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
    case MVM_OP_smrt_strify: return &MVM_coerce_smart_stringify;
    case MVM_OP_box_i: return &MVM_box_int;
    case MVM_OP_box_s: return &MVM_box_str;
    case MVM_OP_box_n: return &MVM_box_num;
    case MVM_OP_unbox_i: return &MVM_repr_get_int;
    case MVM_OP_unbox_s: return &MVM_repr_get_str;
    case MVM_OP_unbox_n: return &MVM_repr_get_num;
    case MVM_OP_istrue: case MVM_OP_isfalse: return &MVM_coerce_istrue;
    case MVM_OP_istype: return &MVM_6model_istype;
    case MVM_OP_isint: case MVM_OP_isnum: case MVM_OP_isstr: // continued
    case MVM_OP_islist: case MVM_OP_ishash: return &MVM_repr_compare_repr_id;
    case MVM_OP_wval: case MVM_OP_wval_wide: return &MVM_sc_get_sc_object;
    case MVM_OP_iter: return &MVM_iter;
    case MVM_OP_iterkey_s: return &MVM_iterkey_s;
    case MVM_OP_iterval: return &MVM_iterval;
    case MVM_OP_push_i: return &MVM_repr_push_i;
    case MVM_OP_push_n: return &MVM_repr_push_n;
    case MVM_OP_push_s: return &MVM_repr_push_s;
    case MVM_OP_push_o: return &MVM_repr_push_o;
    case MVM_OP_unshift_o: return &MVM_repr_unshift_o;
    case MVM_OP_unshift_i: return &MVM_repr_unshift_i;
    case MVM_OP_pop_o: return &MVM_repr_pop_o;
    case MVM_OP_shift_o: return &MVM_repr_shift_o;
    case MVM_OP_pop_i: return &MVM_repr_pop_i;
    case MVM_OP_shift_i: return &MVM_repr_shift_i;
    case MVM_OP_existskey: return &MVM_repr_exists_key;
    case MVM_OP_setelemspos: return &MVM_repr_pos_set_elems;
    case MVM_OP_splice: return &MVM_repr_pos_splice;
    case MVM_OP_atpos_o: return &MVM_repr_at_pos_o;
    case MVM_OP_atpos_i: return &MVM_repr_at_pos_i;
    case MVM_OP_atkey_o: return &MVM_repr_at_key_o;
    case MVM_OP_bindpos_o: return &MVM_repr_bind_pos_o;
    case MVM_OP_bindpos_i: return &MVM_repr_bind_pos_i;
    case MVM_OP_bindkey_o: return &MVM_repr_bind_key_o;
    case MVM_OP_getattr_s: return &MVM_repr_get_attr_s;
    case MVM_OP_getattr_n: return &MVM_repr_get_attr_n;
    case MVM_OP_getattr_i: return &MVM_repr_get_attr_i;
    case MVM_OP_getattr_o: return &MVM_repr_get_attr_o;
    case MVM_OP_bindattr_i: case MVM_OP_bindattr_n: case MVM_OP_bindattr_s: case MVM_OP_bindattr_o: return &MVM_repr_bind_attr_inso;
    case MVM_OP_elems: return &MVM_repr_elems;
    case MVM_OP_flattenropes: return &MVM_string_flatten;
    case MVM_OP_concat_s: return &MVM_string_concatenate;
    case MVM_OP_repeat_s: return &MVM_string_repeat;
    case MVM_OP_flip: return &MVM_string_flip;
    case MVM_OP_uc: return &MVM_string_uc;
    case MVM_OP_tc: return &MVM_string_tc;
    case MVM_OP_lc: return &MVM_string_lc;
    case MVM_OP_eq_s: return &MVM_string_equal;
    case MVM_OP_eqat_s: return &MVM_string_equal_at;
    case MVM_OP_chars: case MVM_OP_graphs_s: return &MVM_string_graphs;
    case MVM_OP_codes_s: return &MVM_string_codes;
    case MVM_OP_index_s: return &MVM_string_index;
    case MVM_OP_substr_s: return &MVM_string_substring;
    case MVM_OP_iscclass: return &MVM_string_is_cclass;
    case MVM_OP_nfarunalt: return &MVM_nfa_run_alt;
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
            ann->type == MVM_SPESH_ANN_DEOPT_INLINE) {
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

static MVMint32 jgb_consume_invoke(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                   MVMSpeshIns *ins) {
    MVMJitNode      *node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    MVMCompUnit       *cu = jgb->sg->sf->body.cu;
    MVMint16 callsite_idx = ins->operands[0].callsite_idx;
    MVMCallsite       *cs = cu->body.callsites[callsite_idx];
    MVMSpeshIns **arg_ins = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshIns*) * cs->arg_count);
    MVMint16            i = 0;
    MVMSpeshBB   *next_bb;
    MVMint32      reentry_label;
    MVMReturnType return_type;
    MVMint16      return_register;
    MVMint16      code_register;
    MVMint16      spesh_cand;
    MVMint16      is_fast;

    while (ins = ins->next) {
        switch(ins->info->opcode) {
        case MVM_OP_arg_i:
        case MVM_OP_arg_n:
        case MVM_OP_arg_s:
        case MVM_OP_arg_o:
        case MVM_OP_argconst_i:
        case MVM_OP_argconst_n:
        case MVM_OP_argconst_s:
            MVM_jit_log(tc, "Invoke arg: <%s>\n", ins->info->name);
            arg_ins[i++] = ins;
            break;
        case MVM_OP_invoke_v:
            return_type     = MVM_RETURN_VOID;
            return_register = -1;
            code_register   = ins->operands[0].reg.orig;
            spesh_cand      = -1;
            is_fast         = 0;
            goto checkargs;
        case MVM_OP_invoke_i:
            return_type     = MVM_RETURN_INT;
            return_register = ins->operands[0].reg.orig;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = -1;
            is_fast         = 0;
            goto checkargs;
        case MVM_OP_invoke_n:
            return_type     = MVM_RETURN_NUM;
            return_register = ins->operands[0].reg.orig;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = -1;
            is_fast         = 0;
            goto checkargs;
        case MVM_OP_invoke_s:
            return_type     = MVM_RETURN_STR;
            return_register = ins->operands[0].reg.orig;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = -1;
            is_fast         = 0;
            goto checkargs;
        case MVM_OP_invoke_o:
            return_type     = MVM_RETURN_OBJ;
            return_register = ins->operands[0].reg.orig;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = -1;
            is_fast         = 0;
            goto checkargs;
        case MVM_OP_sp_fastinvoke_v:
            return_type     = MVM_RETURN_VOID;
            return_register = -1;
            code_register   = ins->operands[0].reg.orig;
            spesh_cand      = ins->operands[1].lit_i16;
            is_fast         = 1;
            goto checkargs;
        case MVM_OP_sp_fastinvoke_o:
            return_type     = MVM_RETURN_OBJ;
            return_register = ins->operands[0].reg.orig;;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = ins->operands[2].lit_i16;
            is_fast         = 1;
            goto checkargs;
        case MVM_OP_sp_fastinvoke_s:
            return_type     = MVM_RETURN_STR;
            return_register = ins->operands[0].reg.orig;;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = ins->operands[2].lit_i16;
            is_fast         = 1;
            goto checkargs;
        case MVM_OP_sp_fastinvoke_i:
            return_type     = MVM_RETURN_INT;
            return_register = ins->operands[0].reg.orig;;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = ins->operands[2].lit_i16;
            is_fast         = 1;
            goto checkargs;
        case MVM_OP_sp_fastinvoke_n:
            return_type     = MVM_RETURN_NUM;
            return_register = ins->operands[0].reg.orig;;
            code_register   = ins->operands[1].reg.orig;
            spesh_cand      = ins->operands[2].lit_i16;
            is_fast         = 1;
            goto checkargs;
        default:
            MVM_jit_log(tc, "Unexpected opcode in invoke sequence: <%s>\n",
                        ins->info->name);
            return 0;
        }
    }
 checkargs:
    if (!ins || i < cs->arg_count) {
        MVM_jit_log(tc, "Could not find invoke opcode or enough arguments\n"
                    "BAIL: op <%s>, expected args: %d, num of args: %d\n",
                    ins->info->name, i, cs->arg_count);
        return 0;
    }
    if (ins != jgb->cur_bb->last_ins || jgb->cur_bb->linear_next == NULL) {
        MVM_jit_log(tc, "Invoke instruction isn't last of basic block or is last of graph\n");
        return 0;
    }
    MVM_jit_log(tc, "Invoke instruction: <%s>\n", ins->info->name);
    next_bb       = jgb->cur_bb->linear_next;
    reentry_label = get_label_name(tc, jgb, next_bb);
    /* create node */
    node->type                     = MVM_JIT_NODE_INVOKE;
    node->u.invoke.callsite_idx    = callsite_idx;
    node->u.invoke.arg_count       = cs->arg_count;
    node->u.invoke.arg_ins         = arg_ins;
    node->u.invoke.return_type     = return_type;
    node->u.invoke.return_register = return_register;
    node->u.invoke.code_register   = code_register;
    node->u.invoke.spesh_cand      = spesh_cand;
    node->u.invoke.reentry_label   = reentry_label;
    node->u.invoke.is_fast         = is_fast;
    jgb_append_node(jgb, node);
    /* move forward to invoke ins */
    jgb->cur_ins = ins;
    return 1;
}

static void jgb_append_control(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                MVMSpeshIns *ins, MVMJitControlType ctrl) {
    MVMJitNode *node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_CONTROL;
    node->u.control.ins  = ins;
    node->u.control.type = ctrl;
    jgb_append_node(jgb, node);
}

static MVMint32 jgb_consume_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                MVMSpeshIns *ins) {
    MVMint16 op = ins->info->opcode;
    MVMSpeshAnn *ann = ins->annotations;

    /* Search annotations for OSR point */
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_DEOPT_OSR) {
            if (jgb->num_offsets == jgb->alloc_offsets)
                /* I want you to laugh and shake your head at this ridiculous line. After that, I will
                 * refactor it into something you'll be able to read. */
                jgb->osr_offsets = memcpy(MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMint32) * (jgb->alloc_offsets *= 2)),
                                          jgb->osr_offsets, jgb->num_offsets * sizeof(MVMint32));
            /* While we're at, it, why not make this a one-liner too */
            jgb->osr_offsets[jgb->num_offsets++] = jgb->sg->deopt_addrs[ann->data.deopt_idx * 2];
            jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_OSRLABEL);
            break;
        }
        ann = ann->next;
    }

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
        /* number coercion */
    case MVM_OP_coerce_ni:
    case MVM_OP_coerce_in:
        /* comparison (integer) */
    case MVM_OP_eq_i:
    case MVM_OP_ne_i:
    case MVM_OP_lt_i:
    case MVM_OP_le_i:
    case MVM_OP_gt_i:
    case MVM_OP_ge_i:
        /* comparison (objects) */
    case MVM_OP_eqaddr:
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
    case MVM_OP_getwho:
    case MVM_OP_getwhere:
    case MVM_OP_sp_getspeshslot:
    case MVM_OP_takedispatcher:
    case MVM_OP_setdispatcher:
    case MVM_OP_getcode:
    case MVM_OP_sp_fastcreate:
    case MVM_OP_decont:
    case MVM_OP_sp_namedarg_used:
    case MVM_OP_hllboxtype_i:
    case MVM_OP_hllboxtype_n:
    case MVM_OP_hllboxtype_s:
    case MVM_OP_null_s:
    case MVM_OP_isnull_s:
        jgb_append_primitive(tc, jgb, ins);
        break;
        /* branches */
    case MVM_OP_goto:
    case MVM_OP_if_i:
    case MVM_OP_unless_i:
    case MVM_OP_ifnonnull:
        jgb_append_branch(tc, jgb, 0, ins);
        break;
    case MVM_OP_if_o:
    case MVM_OP_unless_o: {
        /* Very special / funky branches. The function involved in
         * making this decision - namely, MVM_coerse_istrue - expects
         * to take a return register address /or/ two bytecode
         * addresses.  This is a reasonable decision with regards to
         * invokation nesting in the interpreter, but not for the
         * JIT. Hence, we will transform this into the istrue /
         * isfalse primitive combined with the if_i branch. A special
         * problem is that there really isn't any 'real' work space
         * available to store the result. Instead, we'll use the
         * args space to store and read the result */
        MVMint16 obj = ins->operands[0].reg.orig;
        /* Assign the very last register allocated */
        MVMint16 dst = jgb->sg->num_locals + jgb->sg->sf->body.cu->body.max_callsite_size - 1;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL,  obj },
                                 { MVM_JIT_REG_ADDR, dst }, // destination register (in args space)
                                 { MVM_JIT_LITERAL, 0 }, // true code
                                 { MVM_JIT_LITERAL, 0 }, // false code
                                 { MVM_JIT_LITERAL, op == MVM_OP_unless_o }}; // switch
        MVMSpeshIns * branch = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshIns));
        if (dst + 1 <= jgb->sg->num_locals) {
            MVM_exception_throw_adhoc(tc, "JIT: no space in args buffer to store"
                                      " temporary result for <%s>", ins->info->name);
        }
        jgb_append_call_c(tc, jgb, op_to_func(tc, MVM_OP_istrue), 6,
                          args, MVM_JIT_RV_VOID, -1);
        /* guard the potential invoke */
        jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_INVOKISH);
        /* branch if true (switch is done by coercion) */
        branch->info = MVM_op_get_op(MVM_OP_if_i);
        branch->operands = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshOperand) * 2);
        branch->operands[0].reg.orig = dst;
        branch->operands[1].ins_bb = ins->operands[1].ins_bb;
        jgb_append_branch(tc, jgb, 0, branch);
        break;
    }
    case MVM_OP_istype: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, type },
                                 { MVM_JIT_REG_ADDR, dst }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_isint:
    case MVM_OP_isnum:
    case MVM_OP_isstr:
    case MVM_OP_islist:
    case MVM_OP_ishash: {
        /* XXX This is actually so simple, that it ought to have code
         * generated by the jit directly rather than having a C call. */
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint32 reprid = op == MVM_OP_isint ? MVM_REPR_ID_P6int :
                          op == MVM_OP_isnum ? MVM_REPR_ID_P6num :
                          op == MVM_OP_isstr ? MVM_REPR_ID_P6str :
                          op == MVM_OP_islist ? MVM_REPR_ID_MVMArray :
                     /*  op == MVM_OP_ishash */ MVM_REPR_ID_MVMHash;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, reprid } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
        /* some functions */
    case MVM_OP_checkarity: {
        MVMuint16 min = ins->operands[0].lit_i16;
        MVMuint16 max = ins->operands[1].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_PARAMS },
                                 { MVM_JIT_LITERAL, min },
                                 { MVM_JIT_LITERAL, max } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
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
        MVMint64 idx = op == MVM_OP_wval
            ? ins->operands[2].lit_i16
            : ins->operands[2].lit_i64;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_CU },
                                 { MVM_JIT_LITERAL, dep },
                                 { MVM_JIT_LITERAL, idx } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_isfalse:
    case MVM_OP_istrue: {
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL,  obj },
                                 { MVM_JIT_REG_ADDR, dst },
                                 { MVM_JIT_LITERAL, 0 },
                                 { MVM_JIT_LITERAL, 0 },
                                 { MVM_JIT_LITERAL, op == MVM_OP_isfalse }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 6, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_isnull: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_takeclosure: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_newlexotic: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 offs = ins->operands[1].ins_offset;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_LITERAL, offs } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_flattenropes: {
        MVMint32 target = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, target } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
        /* repr ops */
    case MVM_OP_unshift_i:
    case MVM_OP_push_i:
    case MVM_OP_push_n:
    case MVM_OP_push_s:
    case MVM_OP_unshift_o:
    case MVM_OP_push_o: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 value    = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, value } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_shift_o:
    case MVM_OP_pop_o: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_shift_i:
    case MVM_OP_pop_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_existskey: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 key = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_setelemspos: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 elements = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, elements } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_splice: {
        MVMint16 invocant = ins->operands[0].reg.orig;
        MVMint16 source = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].reg.orig;
        MVMint16 count = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, source },
                                 { MVM_JIT_REG_VAL, offset },
                                 { MVM_JIT_REG_VAL, count } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_atpos_o: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 position = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, position } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_atpos_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 position = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, position } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_atkey_o: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 key = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindpos_i:
    case MVM_OP_bindpos_o:
    case MVM_OP_bindkey_o: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_pos = ins->operands[1].reg.orig;
        MVMint32 value = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_pos },
                                 { MVM_JIT_REG_VAL, value } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getattr_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMuint32 str_idx = ins->operands[3].lit_str_idx;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_getattr_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMuint32 str_idx = ins->operands[3].lit_str_idx;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_getattr_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMuint32 str_idx = ins->operands[3].lit_str_idx;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getattr_o: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMuint32 str_idx = ins->operands[3].lit_str_idx;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindattr_i:
    case MVM_OP_bindattr_n:
    case MVM_OP_bindattr_s:
    case MVM_OP_bindattr_o: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[1].reg.orig;
        MVMuint32 str_idx = ins->operands[2].lit_str_idx;
        MVMint16 val = ins->operands[3].reg.orig;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMuint16 kind = op == MVM_OP_bindattr_i ? MVM_reg_int64 :
                         op == MVM_OP_bindattr_n ? MVM_reg_num64 :
                         op == MVM_OP_bindattr_s ? MVM_reg_str :
                         /* MVM_OP_bindattr_o ? */ MVM_reg_obj;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint },
                                 { MVM_JIT_REG_VAL, val },
                                 { MVM_JIT_LITERAL, kind } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_elems: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
   /* case MVM_OP_iterkey_s:
    case MVM_OP_iterval:
    case MVM_OP_iter: {
        MVMint16 dst      = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }*/
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

    case MVM_OP_smrt_strify:
    case MVM_OP_smrt_numify: {
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_ADDR, dst }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_box_n:
    case MVM_OP_box_s:
    case MVM_OP_box_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 val = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , MVM_JIT_INTERP_TC },
                                 { op == MVM_OP_box_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, val },
                                 { MVM_JIT_REG_VAL, type },
                                 { MVM_JIT_REG_ADDR, dst }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_unbox_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_unbox_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_unbox_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* string ops */
    case MVM_OP_repeat_s:
    case MVM_OP_concat_s: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src_a },
                                 { MVM_JIT_REG_VAL, src_b } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_uc:
    case MVM_OP_lc:
    case MVM_OP_tc: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 string = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, string } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_eq_s: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src_a },
                                 { MVM_JIT_REG_VAL, src_b } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_eqat_s: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src_a },
                                 { MVM_JIT_REG_VAL, src_b },
                                 { MVM_JIT_REG_VAL, offset } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_chars:
    case MVM_OP_graphs_s:
    case MVM_OP_codes_s:
    case MVM_OP_flip: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src } };
        MVMJitRVMode rv_mode = (op == MVM_OP_flip ? MVM_JIT_RV_PTR : MVM_JIT_RV_INT);
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
    case MVM_OP_substr_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 string = ins->operands[1].reg.orig;
        MVMint16 start = ins->operands[2].reg.orig;
        MVMint16 length = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, string },
                                 { MVM_JIT_REG_VAL, start },
                                 { MVM_JIT_REG_VAL, length } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_index_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 haystack = ins->operands[1].reg.orig;
        MVMint16 needle = ins->operands[2].reg.orig;
        MVMint16 start = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, haystack },
                                 { MVM_JIT_REG_VAL, needle },
                                 { MVM_JIT_REG_VAL, start } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_iscclass: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 cclass = ins->operands[1].reg.orig;
        MVMint16 str    = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, cclass },
                                 { MVM_JIT_REG_VAL, str },
                                 { MVM_JIT_REG_VAL, offset } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_nfarunalt: {
        MVMint16 nfa    = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].reg.orig;
        MVMint16 bstack = ins->operands[3].reg.orig;
        MVMint16 cstack = ins->operands[4].reg.orig;
        MVMint16 labels = ins->operands[5].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, nfa },
                                 { MVM_JIT_REG_VAL, target },
                                 { MVM_JIT_REG_VAL, offset },
                                 { MVM_JIT_REG_VAL, bstack },
                                 { MVM_JIT_REG_VAL, cstack },
                                 { MVM_JIT_REG_VAL, labels } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
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
            args[1].type = MVM_JIT_REG_VAL_F;
        }
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        jgb_append_branch(tc, jgb, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
        jgb_append_guard(tc, jgb, ins);
        break;
    case MVM_OP_prepargs: {
        return jgb_consume_invoke(tc, jgb, ins);
    }
    default:
        MVM_jit_log(tc, "BAIL: op <%s>\n", ins->info->name);
        return 0;
    }
    /* If we've consumed an invokish op, we should append a guard */
    if (ins->info->invokish) {
        MVM_jit_log(tc, "append invokish control guard\n");
        jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_INVOKISH);
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
    jg->sg             = jgb->sg;
    jg->num_labels     = jgb->num_labels;
    jg->labels         = jgb->labels;
    jg->first_node     = jgb->first_node;
    jg->last_node      = jgb->last_node;
    jg->num_osr_labels = jgb->num_offsets;
    jg->osr_offsets    = jgb->osr_offsets;
    return jg;
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    JitGraphBuilder jgb;
    MVMJitGraph * jg;
    int i;
    if (!MVM_jit_support()) {
        return NULL;
    }
    {
        MVMuint8 *cuuid = MVM_string_ascii_encode_any(tc, sg->sf->body.cuuid);
        MVMuint8 *name  = MVM_string_ascii_encode_any(tc, sg->sf->body.name);
        MVM_jit_log(tc, "Constructing JIT graph (cuuid: %s, name: '%s')\n",
                    cuuid, name);
        free(cuuid);
        free(name);
    }

    jgb.sg = sg;
    jgb.num_labels = sg->num_bbs;
    jgb.labels = MVM_spesh_alloc(tc, sg, sizeof(MVMJitLabel) * sg->num_bbs);
    /* ignore first BB, which always contains a NOP */
    jgb.cur_bb = sg->entry->linear_next;
    jgb.cur_ins = jgb.cur_bb->first_ins;
    jgb.first_node = jgb.last_node = NULL;
    /* Allocate space for the address -> label mapping needed for OSR */
    jgb.num_offsets = 0;
    jgb.alloc_offsets = 2;
    jgb.osr_offsets = MVM_spesh_alloc(tc, sg, sizeof(MVMint32) * jgb.alloc_offsets);
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
