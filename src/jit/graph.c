#include "moar.h"
#include "math.h"

typedef struct {
    MVMSpeshGraph *sg;
    MVMSpeshBB    *cur_bb;
    MVMSpeshIns   *cur_ins;

    MVMJitNode    *first_node;
    MVMJitNode    *last_node;

    MVMint32      num_labels;
    void        **labeleds;

    MVMint32       num_bbs;
    MVMint32      *bb_labels;

    MVMint32       num_deopts;
    MVMint32       alloc_deopts;
    MVMJitDeopt   *deopts;

    MVMint32       num_handlers;
    MVMJitHandler *handlers;

    MVMint32       num_inlines;
    MVMJitInline  *inlines;
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


static MVMint32 get_label_for_obj(MVMThreadContext *tc, JitGraphBuilder *jgb,
                           void * obj) {
    MVMint32 i;
    for (i = 0; i < jgb->num_labels; i++) {
        if (!jgb->labeleds[i])
            break;
        if (jgb->labeleds[i] == obj)
            return i;
    }
    if (i == jgb->num_labels) {
        void **lblds = MVM_spesh_alloc(tc, jgb->sg, sizeof(void*) * jgb->num_labels * 2);
        memcpy(lblds, jgb->labeleds, jgb->num_labels * sizeof(void*));
        jgb->labeleds = lblds;
        jgb->num_labels *= 2;
    }
    jgb->labeleds[i] = obj;
    return i;
}

static MVMint32 get_label_for_bb(MVMThreadContext *tc, JitGraphBuilder *jgb,
                          MVMSpeshBB *bb) {
    MVMint32 label = get_label_for_obj(tc, jgb, bb);
    jgb->bb_labels[bb->idx] = label;
    return label;
}

/* This is the label that is appended at the very end */
static MVMint32 get_label_for_graph(MVMThreadContext *tc, JitGraphBuilder *jgb,
                             MVMSpeshGraph *sg) {
    return get_label_for_obj(tc, jgb, sg);
}

/* The idea here is that labels are always - in principle - meant before a target. */
static MVMint32 get_label_for_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
                           MVMSpeshBB *bb, MVMSpeshIns *ins, MVMint32 post) {
    if (!post) {
        /* Disregard PHI ops */
        while (ins->prev && ins->prev->info->opcode == MVM_SSA_PHI)
            ins = ins->prev;
        if (ins == bb->first_ins) {
            return get_label_for_obj(tc, jgb, bb);
        }
        return get_label_for_obj(tc, jgb, ins);
    }
    else {
        if (ins->next) {
            return get_label_for_obj(tc, jgb, ins->next);
        }
        else if (bb->linear_next) {
            return get_label_for_obj(tc, jgb, bb->linear_next);
        }
        else { /* end of graph label is identified by the graph itself */
            return get_label_for_graph(tc, jgb, jgb->sg);
        }
    }
}


static void add_deopt_idx(MVMThreadContext *tc, JitGraphBuilder *jgb, MVMint32 label_name, MVMint32 deopt_idx) {
    if (jgb->num_deopts == jgb->alloc_deopts) {
        MVMJitDeopt *deopts = MVM_spesh_alloc(tc, jgb->sg, jgb->alloc_deopts * 2 * sizeof(MVMJitDeopt));
        memcpy(deopts, jgb->deopts, jgb->num_deopts * sizeof(MVMJitDeopt));
        jgb->deopts = deopts;
        jgb->alloc_deopts *= 2;
    }
    jgb->deopts[jgb->num_deopts].label = label_name;
    jgb->deopts[jgb->num_deopts].idx   = deopt_idx;
    jgb->num_deopts++;
}


static void jgb_append_branch(MVMThreadContext *tc, JitGraphBuilder *jgb,
                              MVMint32 name, MVMSpeshIns *ins) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_BRANCH;
    if (ins == NULL) {
        node->u.branch.ins = NULL;
        node->u.branch.dest = name;
    }
    else {
        MVMSpeshBB *bb;
        node->u.branch.ins = ins;
        if (ins->info->opcode == MVM_OP_goto) {
            bb = ins->operands[0].ins_bb;
        }
        else if (ins->info->opcode == MVM_OP_indexat ||
                 ins->info->opcode == MVM_OP_indexnat) {
            bb = ins->operands[3].ins_bb;
        }
        else {
            bb = ins->operands[1].ins_bb;
        }
        node->u.branch.dest = get_label_for_bb(tc, jgb, bb);
    }
    jgb_append_node(jgb, node);
}

static void jgb_append_label(MVMThreadContext *tc, JitGraphBuilder *jgb, MVMint32 name) {
    MVMJitNode *node;
    if (jgb->last_node &&
        jgb->last_node->type == MVM_JIT_NODE_LABEL &&
        jgb->last_node->u.label.name == name)
        return; /* don't double-add labels, even if it may be harmless */
    node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_LABEL;
    node->u.label.name = name;
    jgb_append_node(jgb, node);
    MVM_jit_log(tc, "append label: %d\n", node->u.label.name);
}

static void * op_to_func(MVMThreadContext *tc, MVMint16 opcode) {
    switch(opcode) {
    case MVM_OP_checkarity: return &MVM_args_checkarity;
    case MVM_OP_say: return &MVM_string_say;
    case MVM_OP_print: return &MVM_string_print;
    case MVM_OP_isnull: return &MVM_is_null;
    case MVM_OP_capturelex: return &MVM_frame_capturelex;
    case MVM_OP_takeclosure: return &MVM_frame_takeclosure;
    case MVM_OP_newlexotic: return &MVM_exception_newlexotic_from_jit;
    case MVM_OP_usecapture: return &MVM_args_use_capture;
    case MVM_OP_savecapture: return &MVM_args_save_capture;
    case MVM_OP_return: return &MVM_args_assert_void_return_ok;
    case MVM_OP_return_i: return &MVM_args_set_result_int;
    case MVM_OP_return_s: return &MVM_args_set_result_str;
    case MVM_OP_return_o: return &MVM_args_set_result_obj;
    case MVM_OP_return_n: return &MVM_args_set_result_num;
    case MVM_OP_coerce_is: return &MVM_coerce_i_s;
    case MVM_OP_coerce_ns: return &MVM_coerce_n_s;
    case MVM_OP_coerce_si: return &MVM_coerce_s_i;
    case MVM_OP_coerce_sn: return &MVM_coerce_s_n;
    case MVM_OP_coerce_In: return &MVM_bigint_to_num;
    case MVM_OP_iterkey_s: return &MVM_iterkey_s;
    case MVM_OP_iter: return &MVM_iter;
    case MVM_OP_iterval: return &MVM_iterval;
    case MVM_OP_die: return &MVM_exception_die;
    case MVM_OP_smrt_numify: return &MVM_coerce_smart_numify;
    case MVM_OP_smrt_strify: return &MVM_coerce_smart_stringify;
    case MVM_OP_write_fhs: return &MVM_io_write_string;
    case MVM_OP_gethow: return &MVM_6model_get_how_obj;
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
    case MVM_OP_getdynlex: return &MVM_frame_getdynlex;
    case MVM_OP_binddynlex: return &MVM_frame_binddynlex;
    case MVM_OP_getlexouter: return &MVM_frame_find_lexical_by_name_outer;
    case MVM_OP_findmeth: case MVM_OP_findmeth_s: return &MVM_6model_find_method;
    case MVM_OP_multicacheadd: return &MVM_multi_cache_add;
    case MVM_OP_multicachefind: return &MVM_multi_cache_find;
    case MVM_OP_can: case MVM_OP_can_s: return &MVM_6model_can_method;
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
    case MVM_OP_deletekey: return &MVM_repr_delete_key;
    case MVM_OP_setelemspos: return &MVM_repr_pos_set_elems;
    case MVM_OP_splice: return &MVM_repr_pos_splice;
    case MVM_OP_atpos_i: return &MVM_repr_at_pos_i;
    case MVM_OP_atpos_n: return &MVM_repr_at_pos_n;
    case MVM_OP_atpos_s: return &MVM_repr_at_pos_s;
    case MVM_OP_atpos_o: return &MVM_repr_at_pos_o;
    case MVM_OP_existspos: return &MVM_repr_exists_pos;
    case MVM_OP_atkey_o: return &MVM_repr_at_key_o;
    case MVM_OP_bindpos_o: return &MVM_repr_bind_pos_o;
    case MVM_OP_bindpos_i: return &MVM_repr_bind_pos_i;
    case MVM_OP_bindpos_n: return &MVM_repr_bind_pos_n;
    case MVM_OP_bindpos_s: return &MVM_repr_bind_pos_s;
    case MVM_OP_bindkey_o: return &MVM_repr_bind_key_o;
    case MVM_OP_getattr_s: return &MVM_repr_get_attr_s;
    case MVM_OP_getattr_n: return &MVM_repr_get_attr_n;
    case MVM_OP_getattr_i: return &MVM_repr_get_attr_i;
    case MVM_OP_getattr_o: return &MVM_repr_get_attr_o;
    case MVM_OP_getattrs_s: return &MVM_repr_get_attr_s;
    case MVM_OP_getattrs_n: return &MVM_repr_get_attr_n;
    case MVM_OP_getattrs_i: return &MVM_repr_get_attr_i;
    case MVM_OP_getattrs_o: return &MVM_repr_get_attr_o;
    case MVM_OP_bindattr_i: case MVM_OP_bindattr_n: case MVM_OP_bindattr_s: case MVM_OP_bindattr_o: return &MVM_repr_bind_attr_inso;
    case MVM_OP_bindattrs_i: case MVM_OP_bindattrs_n: case MVM_OP_bindattrs_s: case MVM_OP_bindattrs_o: return &MVM_repr_bind_attr_inso;
    case MVM_OP_elems: return &MVM_repr_elems;
    case MVM_OP_flattenropes: return &MVM_string_flatten;
    case MVM_OP_concat_s: return &MVM_string_concatenate;
    case MVM_OP_repeat_s: return &MVM_string_repeat;
    case MVM_OP_flip: return &MVM_string_flip;
    case MVM_OP_split: return &MVM_string_split;
    case MVM_OP_escape: return &MVM_string_escape;
    case MVM_OP_uc: return &MVM_string_uc;
    case MVM_OP_tc: return &MVM_string_tc;
    case MVM_OP_lc: return &MVM_string_lc;
    case MVM_OP_eq_s: return &MVM_string_equal;
    case MVM_OP_eqat_s: return &MVM_string_equal_at;
    case MVM_OP_ordat: return &MVM_string_get_grapheme_at;
    case MVM_OP_chars: case MVM_OP_graphs_s: return &MVM_string_graphs;
    case MVM_OP_codes_s: return &MVM_string_codes;
    case MVM_OP_index_s: return &MVM_string_index;
    case MVM_OP_substr_s: return &MVM_string_substring;
    case MVM_OP_join: return &MVM_string_join;
    case MVM_OP_iscclass: return &MVM_string_is_cclass;
    case MVM_OP_findcclass: return &MVM_string_find_cclass;
    case MVM_OP_findnotcclass: return &MVM_string_find_not_cclass;
    case MVM_OP_nfarunalt: return &MVM_nfa_run_alt;
    case MVM_OP_nfarunproto: return &MVM_nfa_run_proto;
    case MVM_OP_nfafromstatelist: return &MVM_nfa_from_statelist;
    case MVM_OP_hllize: return &MVM_hll_map;
    case MVM_OP_clone: return &MVM_repr_clone;
    case MVM_OP_getcodeobj: return &MVM_frame_get_code_object;
    case MVM_OP_isbig_I: return &MVM_bigint_is_big;
    case MVM_OP_cmp_I: return &MVM_bigint_cmp;
    case MVM_OP_add_I: return &MVM_bigint_add;
    case MVM_OP_sub_I: return &MVM_bigint_sub;
    case MVM_OP_mul_I: return &MVM_bigint_mul;
    case MVM_OP_div_I: return &MVM_bigint_div;
    case MVM_OP_mod_I: return &MVM_bigint_mod;
    case MVM_OP_lcm_I: return &MVM_bigint_lcm;
    case MVM_OP_gcd_I: return &MVM_bigint_gcd;
    case MVM_OP_bool_I: return &MVM_bigint_bool;
    case MVM_OP_div_In: return &MVM_bigint_div_num;
    case MVM_OP_coerce_Is: case MVM_OP_base_I: return &MVM_bigint_to_str;
    case MVM_OP_radix_I: return &MVM_bigint_radix;
    case MVM_OP_sqrt_n: return &sqrt;
    case MVM_OP_sin_n: return &sin;
    case MVM_OP_cos_n: return &cos;
    case MVM_OP_tan_n: return &tan;
    case MVM_OP_asin_n: return &asin;
    case MVM_OP_acos_n: return &acos;
    case MVM_OP_atan_n: return &atan;
    case MVM_OP_atan2_n: return &atan2;
    case MVM_OP_pow_n: return &pow;
    case MVM_OP_time_n: return &MVM_proc_time_n;
    case MVM_OP_randscale_n: return &MVM_proc_randscale_n;
    case MVM_OP_isnanorinf: return &MVM_num_isnanorinf;
    case MVM_OP_nativecallinvoke: return &MVM_nativecall_invoke;
    case MVM_OP_iscont_i: return &MVM_6model_container_iscont_i;
    case MVM_OP_iscont_n: return &MVM_6model_container_iscont_n;
    case MVM_OP_iscont_s: return &MVM_6model_container_iscont_s;
    case MVM_OP_assign_i: return &MVM_6model_container_assign_i;
    case MVM_OP_assign_n: return &MVM_6model_container_assign_n;
    case MVM_OP_assign_s: return &MVM_6model_container_assign_s;
    case MVM_OP_decont_i: return &MVM_6model_container_decont_i;
    case MVM_OP_decont_n: return &MVM_6model_container_decont_n;
    case MVM_OP_decont_s: return &MVM_6model_container_decont_s;
    case MVM_OP_getlexref_i: return &MVM_nativeref_lex_i;
    case MVM_OP_getlexref_n: return &MVM_nativeref_lex_n;
    case MVM_OP_getlexref_s: return &MVM_nativeref_lex_s;
    case MVM_OP_sp_boolify_iter: return &MVM_iter_istrue;
    case MVM_OP_prof_allocated: return &MVM_profile_log_allocated;
    case MVM_OP_prof_exit: return &MVM_profile_log_exit;
    default:
        MVM_exception_throw_adhoc(tc, "JIT: No function for op %d in op_to_func.", opcode);
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
    MVMCompUnit       *cu = jgb->sg->sf->body.cu;
    MVMint16 callsite_idx = ins->operands[0].callsite_idx;
    MVMCallsite       *cs = cu->body.callsites[callsite_idx];
    MVMSpeshIns **arg_ins = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshIns*) * cs->arg_count);
    MVMint16            i = 0;
    MVMJitNode      *node;
    MVMint32      reentry_label;
    MVMReturnType return_type;
    MVMint16      return_register;
    MVMint16      code_register;
    MVMint16      spesh_cand;
    MVMint16      is_fast;

    while ((ins = ins->next)) {
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
    MVM_jit_log(tc, "Invoke instruction: <%s>\n", ins->info->name);
    /* get label /after/ current (invoke) ins, where we'll need to reenter the JIT */
    reentry_label = get_label_for_ins(tc, jgb, jgb->cur_bb, ins, 1);
    /* create invoke node */
    node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
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

    /* append reentry label */
    jgb_append_label(tc, jgb, reentry_label);
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

static MVMint32 jgb_consume_jumplist(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                     MVMSpeshIns *ins) {
    MVMint64 num_labels  = ins->operands[0].lit_i64;
    MVMint16 idx_reg     = ins->operands[1].reg.orig;
    MVMint32 *in_labels  = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMint32) * num_labels);
    MVMint32 *out_labels = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMint32) * num_labels);
    MVMSpeshBB *bb       = jgb->cur_bb;
    MVMJitNode *node;
    MVMint64 i;
    for (i = 0; i < num_labels; i++) {
        bb = bb->linear_next; // take the next basic block
        if (!bb || bb->first_ins != bb->last_ins) return 0; //  which must exist
        ins = bb->first_ins;  //  and it's first and only entry
        if (ins->info->opcode != MVM_OP_goto)  // which must be a goto
            return 0;
        in_labels[i]  = get_label_for_bb(tc, jgb, bb);
        out_labels[i] = get_label_for_bb(tc, jgb, ins->operands[0].ins_bb);
    }
    /* build the node */
    node = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_JUMPLIST;
    node->u.jumplist.num_labels = num_labels;
    node->u.jumplist.reg = idx_reg;
    node->u.jumplist.in_labels = in_labels;
    node->u.jumplist.out_labels = out_labels;
    jgb_append_node(jgb, node);
    /* set cur_bb and cur_ins to the end of our jumplist */
    jgb->cur_bb = bb;
    jgb->cur_ins = ins;
    return 1;
}

static MVMuint16 * try_fake_extop_regs(MVMThreadContext *tc, MVMSpeshIns *ins) {
    /* XXX Need to be able to clear these up, some day. */
    MVMuint16 *regs = MVM_calloc(ins->info->num_operands, sizeof(MVMuint16));
    MVMuint16 i;
    for (i = 0; i < ins->info->num_operands; i++) {
        switch (ins->info->operands[i] & MVM_operand_rw_mask) {
        case MVM_operand_read_reg:
        case MVM_operand_write_reg:
            regs[i] = ins->operands[i].reg.orig;
            break;
        default:
            MVM_free(regs);
            return NULL;
        }
    }
    return regs;
}

static void log_inline(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMint32 inline_idx, MVMint32 is_entry) {
    MVMStaticFrame *sf = sg->inlines[inline_idx].code->body.sf;
    char *name         = MVM_string_utf8_encode_C_string(tc, sf->body.name);
    char *cuuid        = MVM_string_utf8_encode_C_string(tc, sf->body.cuuid);
    MVM_jit_log(tc, "%s inline %d (name: %s, cuuid: %s)\n", is_entry ? "Entering" : "Leaving",
                inline_idx, name, cuuid);
    MVM_free(name);
    MVM_free(cuuid);
}

static void jgb_before_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
                           MVMSpeshBB *bb, MVMSpeshIns *ins) {
   MVMSpeshAnn *ann = ins->annotations;

    /* Search annotations for stuff that may need a label. */
    while (ann) {
        switch(ann->type) {
        case MVM_SPESH_ANN_DEOPT_OSR: {
            /* get label before our instruction */
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 0);
            jgb_append_label(tc, jgb, label);
            add_deopt_idx(tc, jgb, label, ann->data.deopt_idx);
            break;
        }
        case MVM_SPESH_ANN_FH_START: {
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 0);
            jgb_append_label(tc, jgb, label);
            jgb->handlers[ann->data.frame_handler_index].start_label = label;
            /* Load the current position into the jit entry label, so that
             * when throwing we'll know which handler to use */
            jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_DYNAMIC_LABEL);
            break;
        }
        case MVM_SPESH_ANN_FH_END: {
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 0);
            jgb_append_label(tc, jgb, label);
            jgb->handlers[ann->data.frame_handler_index].end_label = label;
            /* Same as above. Note that the dynamic label control
             * actually loads a position a few bytes away from the
             * label appended above. This is in this case intentional
             * because the frame handler end is exclusive; once it is
             * passed we should not use the same handler again.  If we
             * loaded the exact same position, we would not be able to
             * distinguish between the end of the basic block to which
             * the handler applies and the start of the basic block to
             * which it doesn't. */
            jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_DYNAMIC_LABEL);
            break;
        }
        case MVM_SPESH_ANN_FH_GOTO: {
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 0);
            jgb_append_label(tc, jgb, label);
            jgb->handlers[ann->data.frame_handler_index].goto_label = label;
            break;
        }
        case MVM_SPESH_ANN_INLINE_START: {
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 0);
            jgb_append_label(tc, jgb, label); 
            jgb->inlines[ann->data.inline_idx].start_label = label;
            if (tc->instance->jit_log_fh)
                log_inline(tc, jgb->sg, ann->data.inline_idx, 1);
            break;
        }
        } /* switch */
        ann = ann->next;
    }
    if (ins->info->jittivity & MVM_JIT_INFO_THROWISH) {
        jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_THROWISH_PRE);
    }
}

static void jgb_after_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
                          MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMSpeshAnn *ann;

    /* If we've consumed an (or throwish) op, we should append a guard */
    if (ins->info->jittivity & MVM_JIT_INFO_INVOKISH) {
        MVM_jit_log(tc, "append invokish control guard\n");
        jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_INVOKISH);
    }
    else if (ins->info->jittivity & MVM_JIT_INFO_THROWISH) {
        jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_THROWISH_POST);
    }
    /* This order of processing is necessary to ensure that a label
     * calculated by one of the control guards as well as the labels
     * calculated below point to the exact same instruction. This is a
     * relatively fragile construction! One could argue that the
     * control guards should in fact use the same (dynamic) labels. */
    ann = ins->annotations;
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_INLINE_END) {
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 1);
            jgb_append_label(tc, jgb, label);
            jgb->inlines[ann->data.inline_idx].end_label = label;
            if (tc->instance->jit_log_fh)
                log_inline(tc, jgb->sg, ann->data.inline_idx, 0);
        } else if (ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS /* ||
                                                               ann->type == MVM_SPESH_ANN_DEOPT_INLINE */) {
            /* An underlying assumption here is that this instruction
             * will in fact set the jit_entry_label to a correct
             * value. This is clearly true for invoking ops as well
             * as invokish ops, and in fact there is no other way
             * to get a deopt_all_ins annotation. Still, be warned. */
            MVMint32 label = get_label_for_ins(tc, jgb, bb, ins, 1);
            jgb_append_label(tc, jgb, label);
            add_deopt_idx(tc, jgb, label, ann->data.deopt_idx);
        }
        ann = ann->next;
    }
}

static MVMint32 jgb_consume_reprop(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                   MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMint16 op = ins->info->opcode;
    MVMSpeshOperand type_operand;
    MVMSpeshFacts *type_facts = 0;
    MVMint32 alternative = 0;

    switch (op) {
        case MVM_OP_unshift_i:
        case MVM_OP_unshift_n:
        case MVM_OP_unshift_s:
        case MVM_OP_unshift_o:
        case MVM_OP_bindkey_i:
        case MVM_OP_bindkey_n:
        case MVM_OP_bindkey_s:
        case MVM_OP_bindkey_o:
        case MVM_OP_bindpos_i:
        case MVM_OP_bindpos_n:
        case MVM_OP_bindpos_s:
        case MVM_OP_bindpos_o:
        case MVM_OP_bindattr_i:
        case MVM_OP_bindattr_n:
        case MVM_OP_bindattr_s:
        case MVM_OP_bindattr_o:
        case MVM_OP_bindattrs_i:
        case MVM_OP_bindattrs_n:
        case MVM_OP_bindattrs_s:
        case MVM_OP_bindattrs_o:
        case MVM_OP_push_i:
        case MVM_OP_push_n:
        case MVM_OP_push_s:
        case MVM_OP_push_o:
        case MVM_OP_deletekey:
        case MVM_OP_setelemspos:
        case MVM_OP_splice:
            type_operand = ins->operands[0];
            break;
        case MVM_OP_atpos_i:
        case MVM_OP_atpos_n:
        case MVM_OP_atpos_s:
        case MVM_OP_atpos_o:
        case MVM_OP_atkey_i:
        case MVM_OP_atkey_n:
        case MVM_OP_atkey_s:
        case MVM_OP_atkey_o:
        case MVM_OP_elems:
        case MVM_OP_shift_i:
        case MVM_OP_shift_n:
        case MVM_OP_shift_s:
        case MVM_OP_shift_o:
        case MVM_OP_pop_i:
        case MVM_OP_pop_n:
        case MVM_OP_pop_s:
        case MVM_OP_pop_o:
        case MVM_OP_existskey:
        case MVM_OP_existspos:
        case MVM_OP_getattr_i:
        case MVM_OP_getattr_n:
        case MVM_OP_getattr_s:
        case MVM_OP_getattr_o:
        case MVM_OP_getattrs_i:
        case MVM_OP_getattrs_n:
        case MVM_OP_getattrs_s:
        case MVM_OP_getattrs_o:
            type_operand = ins->operands[1];
            break;
        case MVM_OP_box_i:
        case MVM_OP_box_n:
        case MVM_OP_box_s:
            type_operand = ins->operands[2];
            break;
        default:
            MVM_jit_log(tc, "devirt: couldn't figure out type operand for op %s\n", ins->info->name);

    }

    type_facts = MVM_spesh_get_facts(tc, jgb->sg, type_operand);

    if (type_facts && type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && type_facts->type) {
        switch(op) {
            case MVM_OP_atkey_i:
            case MVM_OP_atkey_n:
            case MVM_OP_atkey_s:
            case MVM_OP_atkey_o:
                alternative = 1;
            case MVM_OP_atpos_i:
            case MVM_OP_atpos_n:
            case MVM_OP_atpos_s:
            case MVM_OP_atpos_o: {
                /* atpos_i             w(int64) r(obj) r(int64) */
                /* atkey_i             w(int64) r(obj) r(str)*/

                /*void (*at_pos) (MVMThreadContext *tc, MVMSTable *st,
                 *    MVMObject *root, void *data, MVMint64 index,
                 *    MVMRegister *result, MVMuint16 kind);*/

                /*REPR(obj)->pos_funcs.at_pos(tc, STABLE(obj), obj, OBJECT_BODY(obj),
                 *  idx, &value, MVM_reg_int64);*/

                MVMint32 dst      = ins->operands[0].reg.orig;
                MVMint32 invocant = ins->operands[1].reg.orig;
                MVMint32 value    = ins->operands[2].reg.orig;

                void *function = alternative
                    ? (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.at_key
                    : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.at_pos;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,  value },
                                         { MVM_JIT_REG_ADDR, dst },
                                         { MVM_JIT_LITERAL,
                                             op == MVM_OP_atpos_i || op == MVM_OP_atkey_i ? MVM_reg_int64 :
                                             op == MVM_OP_atpos_n || op == MVM_OP_atkey_n ? MVM_reg_num64 :
                                             op == MVM_OP_atpos_s || op == MVM_OP_atkey_s ? MVM_reg_str :
                                                                    MVM_reg_obj } };
                jgb_append_call_c(tc, jgb, function, 7, args, MVM_JIT_RV_VOID, -1);
                MVM_jit_log(tc, "devirt: emitted an %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            case MVM_OP_bindkey_i:
            case MVM_OP_bindkey_n:
            case MVM_OP_bindkey_s:
            case MVM_OP_bindkey_o:
                alternative = 1;
            case MVM_OP_bindpos_i:
            case MVM_OP_bindpos_n:
            case MVM_OP_bindpos_s:
            case MVM_OP_bindpos_o: {
                /*bindpos_i           r(obj) r(int64) r(int64)*/
                /*bindkey_i           r(obj) r(str) r(int64)*/

                /* void (*bind_pos) (MVMThreadContext *tc, MVMSTable *st,
                      MVMObject *root, void *data, MVMint64 index,
                      MVMRegister value, MVMuint16 kind); */

                /* void (*bind_key) (MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                      void *data, MVMObject *key, MVMRegister value, MVMuint16 kind); */

                MVMint32 invocant = ins->operands[0].reg.orig;
                MVMint32 key      = ins->operands[1].reg.orig;
                MVMint32 value    = ins->operands[2].reg.orig;

                void *function = alternative
                    ? (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.bind_key
                    : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.bind_pos;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,  key },
                                         { op == MVM_OP_bindpos_n || op == MVM_OP_bindkey_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL ,
                                                 value },
                                         { MVM_JIT_LITERAL,
                                             op == MVM_OP_bindpos_i || op == MVM_OP_bindkey_i ? MVM_reg_int64 :
                                             op == MVM_OP_bindpos_n || op == MVM_OP_bindkey_n ? MVM_reg_num64 :
                                             op == MVM_OP_bindpos_s || op == MVM_OP_bindkey_s ? MVM_reg_str :
                                                                    MVM_reg_obj } };
                jgb_append_call_c(tc, jgb, function, 7, args, MVM_JIT_RV_VOID, -1);
                MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            case MVM_OP_elems: {
                /*elems               w(int64) r(obj) :pure*/

                MVMint32 dst       = ins->operands[0].reg.orig;
                MVMint32 invocant  = ins->operands[1].reg.orig;

                void *function = ((MVMObject*)type_facts->type)->st->REPR->elems;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant } };
                jgb_append_call_c(tc, jgb, function, 4, args, MVM_JIT_RV_INT, dst);
                MVM_jit_log(tc, "devirt: emitted an elems via jgb_consume_reprop\n");
                return 1;
            }
            case MVM_OP_getattr_i:
            case MVM_OP_getattr_n:
            case MVM_OP_getattr_s:
            case MVM_OP_getattr_o:
            case MVM_OP_getattrs_i:
            case MVM_OP_getattrs_n:
            case MVM_OP_getattrs_s:
            case MVM_OP_getattrs_o: {
                /*getattr_i           w(int64) r(obj) r(obj) str int16*/
                /*getattrs_i          w(int64) r(obj) r(obj) r(str)*/
                /*static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,*/
                /*        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,*/
                /*      MVMRegister *result_reg, MVMuint16 kind) {*/

                /* reprconv and interp.c check for concreteness, so we'd either
                 * have to emit a bit of code to check and throw or just rely
                 * on a concreteness fact */

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, jgb->sg, ins->operands[1]);

                if (object_facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMint32 is_name_direct = ins->info->num_operands == 5;

                    MVMint32 dst       = ins->operands[0].reg.orig;
                    MVMint32 invocant  = ins->operands[1].reg.orig;
                    MVMint32 type      = ins->operands[2].reg.orig;
                    MVMint32 attrname  = is_name_direct ? ins->operands[3].lit_str_idx : ins->operands[3].reg.orig;
                    MVMint32 attrhint  = is_name_direct ? ins->operands[4].lit_i16 : -1;

                    void *function = ((MVMObject*)type_facts->type)->st->REPR->attr_funcs.get_attribute;

                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_VAL,     invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL,     type },
                                             { is_name_direct ? MVM_JIT_STR_IDX : MVM_JIT_REG_VAL,
                                                                    attrname },
                                             { MVM_JIT_LITERAL,     attrhint },
                                             { MVM_JIT_REG_ADDR,    dst },
                                             { MVM_JIT_LITERAL,
                                                 op == MVM_OP_getattr_i || op == MVM_OP_getattrs_i ? MVM_reg_int64 :
                                                 op == MVM_OP_getattr_n || op == MVM_OP_getattrs_n ? MVM_reg_num64 :
                                                 op == MVM_OP_getattr_s || op == MVM_OP_getattrs_s ? MVM_reg_str :
                                                                        MVM_reg_obj } };
                    MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                    jgb_append_call_c(tc, jgb, function, 9, args, MVM_JIT_RV_VOID, -1);

                    return 1;
                } else {
                    MVM_jit_log(tc, "devirt: couldn't %s; concreteness not sure\n", ins->info->name);
                    break;
                }
            }
            case MVM_OP_bindattr_i:
            case MVM_OP_bindattr_n:
            case MVM_OP_bindattr_s:
            case MVM_OP_bindattr_o:
            case MVM_OP_bindattrs_i:
            case MVM_OP_bindattrs_n:
            case MVM_OP_bindattrs_s:
            case MVM_OP_bindattrs_o: {
                /*bindattr_n          r(obj) r(obj) str    r(num64) int16*/
                /*bindattrs_n         r(obj) r(obj) r(str) r(num64)*/

                /* static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
                 *        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
                 *        MVMRegister value_reg, MVMuint16 kind) */

                /* reprconv and interp.c check for concreteness, so we'd either
                 * have to emit a bit of code to check and throw or just rely
                 * on a concreteness fact */

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, jgb->sg, ins->operands[1]);

                if (object_facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMint32 is_name_direct = ins->info->num_operands == 5;

                    MVMint32 invocant  = ins->operands[0].reg.orig;
                    MVMint32 type      = ins->operands[1].reg.orig;
                    MVMint32 attrname  = is_name_direct ? ins->operands[2].lit_str_idx : ins->operands[2].reg.orig;
                    MVMint32 attrhint  = is_name_direct ? ins->operands[4].lit_i16 : -1;
                    MVMint32 value     = ins->operands[3].reg.orig;

                    void *function = ((MVMObject*)type_facts->type)->st->REPR->attr_funcs.bind_attribute;

                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_VAL,     invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL,     type },
                                             { is_name_direct ? MVM_JIT_STR_IDX : MVM_JIT_REG_VAL,
                                                                    attrname },
                                             { MVM_JIT_LITERAL,     attrhint },
                                             { op == MVM_OP_bindattr_n || op == MVM_OP_bindattrs_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL ,
                                                     value },
                                             { MVM_JIT_LITERAL,
                                                 op == MVM_OP_bindattr_i || op == MVM_OP_bindattrs_i ? MVM_reg_int64 :
                                                 op == MVM_OP_bindattr_n || op == MVM_OP_bindattrs_n ? MVM_reg_num64 :
                                                 op == MVM_OP_bindattr_s || op == MVM_OP_bindattrs_s ? MVM_reg_str :
                                                                        MVM_reg_obj } };
                    MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                    jgb_append_call_c(tc, jgb, function, 9, args, MVM_JIT_RV_VOID, -1);

                    return 1;
                } else {
                    MVM_jit_log(tc, "devirt: couldn't %s; concreteness not sure\n", ins->info->name);
                    break;
                }
            }
            case MVM_OP_push_i:
            case MVM_OP_push_n:
            case MVM_OP_push_s:
            case MVM_OP_push_o:
                alternative = 1;
            case MVM_OP_unshift_i:
            case MVM_OP_unshift_n:
            case MVM_OP_unshift_s:
            case MVM_OP_unshift_o: {
                MVMint32 invocant = ins->operands[0].reg.orig;
                MVMint32 value    = ins->operands[1].reg.orig;

                void *function = alternative
                    ? (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.push
                    : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.unshift;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { op == MVM_OP_push_n || op == MVM_OP_unshift_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL ,
                                                 value },
                                         { MVM_JIT_LITERAL,
                                             op == MVM_OP_push_i || op == MVM_OP_unshift_i ? MVM_reg_int64 :
                                             op == MVM_OP_push_n || op == MVM_OP_unshift_n ? MVM_reg_num64 :
                                             op == MVM_OP_push_s || op == MVM_OP_unshift_s ? MVM_reg_str :
                                                                    MVM_reg_obj } };
                jgb_append_call_c(tc, jgb, function, 6, args, MVM_JIT_RV_VOID, -1);
                MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            case MVM_OP_pop_i:
            case MVM_OP_pop_n:
            case MVM_OP_pop_s:
            case MVM_OP_pop_o:
                alternative = 1;
            case MVM_OP_shift_i:
            case MVM_OP_shift_n:
            case MVM_OP_shift_s:
            case MVM_OP_shift_o: {
                MVMint32 dst      = ins->operands[0].reg.orig;
                MVMint32 invocant = ins->operands[1].reg.orig;

                void *function = alternative
                    ? (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.pop
                    : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.shift;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_ADDR,     dst },
                                         { MVM_JIT_LITERAL,
                                             op == MVM_OP_pop_i || op == MVM_OP_shift_i ? MVM_reg_int64 :
                                             op == MVM_OP_pop_n || op == MVM_OP_shift_n ? MVM_reg_num64 :
                                             op == MVM_OP_pop_s || op == MVM_OP_shift_s ? MVM_reg_str :
                                                                    MVM_reg_obj } };
                jgb_append_call_c(tc, jgb, function, 6, args, MVM_JIT_RV_VOID, -1);
                MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            case MVM_OP_setelemspos: {
                MVMint32 invocant = ins->operands[0].reg.orig;
                MVMint32 amount   = ins->operands[1].reg.orig;

                void *function = ((MVMObject*)type_facts->type)->st->REPR->pos_funcs.set_elems;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,     amount } };
                jgb_append_call_c(tc, jgb, function, 5, args, MVM_JIT_RV_VOID, -1);
                MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            case MVM_OP_existspos:
                /*existspos           w(int64) r(obj) r(int64)*/
                alternative = 1;
            case MVM_OP_existskey: {
                /*existskey           w(int64) r(obj) r(str) :pure*/
                MVMint32 dst      = ins->operands[0].reg.orig;
                MVMint32 invocant = ins->operands[1].reg.orig;
                MVMint32 keyidx   = ins->operands[2].reg.orig;

                void *function = alternative
                    ? (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.exists_pos
                    : (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.exists_key;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,     keyidx } };
                jgb_append_call_c(tc, jgb, function, 5, args, MVM_JIT_RV_INT, dst);
                MVM_jit_log(tc, "devirt: emitted a %s via jgb_consume_reprop\n", ins->info->name);
                return 1;
            }
            default:
                MVM_jit_log(tc, "devirt: please implement emitting repr op %s\n", ins->info->name);
        }
    } else {
        MVM_jit_log(tc, "devirt: repr op %s couldn't be devirtualized: type unknown\n", ins->info->name);
    }

    switch(op) {
    case MVM_OP_push_i:
    case MVM_OP_push_s:
    case MVM_OP_push_o:
    case MVM_OP_unshift_i:
    case MVM_OP_unshift_s:
    case MVM_OP_unshift_o: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 value    = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, value } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_unshift_n:
    case MVM_OP_push_n: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 value    = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL_F, value } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_shift_s:
    case MVM_OP_pop_s:
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
    case MVM_OP_shift_n:
    case MVM_OP_pop_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_deletekey:
    case MVM_OP_setelemspos: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_or_val = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_or_val } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
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
    case MVM_OP_existspos:
    case MVM_OP_atkey_i:
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
    case MVM_OP_atkey_n:
    case MVM_OP_atpos_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 position = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, position } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_atpos_o:
    case MVM_OP_atkey_o:
    case MVM_OP_atkey_s:
    case MVM_OP_atpos_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 position = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, position } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindpos_i:
    case MVM_OP_bindpos_n:
    case MVM_OP_bindpos_s:
    case MVM_OP_bindpos_o:
    case MVM_OP_bindkey_i:
    case MVM_OP_bindkey_n:
    case MVM_OP_bindkey_s:
    case MVM_OP_bindkey_o: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_pos = ins->operands[1].reg.orig;
        MVMint32 value = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_pos },
                                 { op == MVM_OP_bindpos_n || op == MVM_OP_bindkey_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, value } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getattr_i:
    case MVM_OP_getattr_n:
    case MVM_OP_getattr_s:
    case MVM_OP_getattr_o: {
        MVMuint16 kind = op == MVM_OP_getattr_i ? MVM_JIT_RV_INT :
                         op == MVM_OP_getattr_n ? MVM_JIT_RV_NUM :
                         op == MVM_OP_getattr_s ? MVM_JIT_RV_PTR :
                         /* MVM_OP_getattr_o ? */ MVM_JIT_RV_PTR;
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
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, kind, dst);
        break;
    }
    case MVM_OP_getattrs_i:
    case MVM_OP_getattrs_n:
    case MVM_OP_getattrs_s:
    case MVM_OP_getattrs_o: {
        MVMuint16 kind = op == MVM_OP_getattrs_i ? MVM_JIT_RV_INT :
                         op == MVM_OP_getattrs_n ? MVM_JIT_RV_NUM :
                         op == MVM_OP_getattrs_s ? MVM_JIT_RV_PTR :
                         /* MVM_OP_getattrs_o ? */ MVM_JIT_RV_PTR;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMint16 str = ins->operands[3].reg.orig;
        MVMint16 hint = -1;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_REG_VAL, str },
                                 { MVM_JIT_LITERAL, hint }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, kind, dst);
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
                                 { op == MVM_OP_bindattr_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, val },
                                 { MVM_JIT_LITERAL, kind } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_bindattrs_i:
    case MVM_OP_bindattrs_n:
    case MVM_OP_bindattrs_s:
    case MVM_OP_bindattrs_o: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[1].reg.orig;
        MVMint16 str = ins->operands[2].reg.orig;
        MVMint16 val = ins->operands[3].reg.orig;
        MVMint16 hint = -1;
        MVMuint16 kind = op == MVM_OP_bindattrs_i ? MVM_reg_int64 :
                         op == MVM_OP_bindattrs_n ? MVM_reg_num64 :
                         op == MVM_OP_bindattrs_s ? MVM_reg_str :
                         /* MVM_OP_bindattrs_o ? */ MVM_reg_obj;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_REG_VAL, str },
                                 { MVM_JIT_LITERAL, hint },
                                 { op == MVM_OP_bindattrs_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, val },
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
    default:
    return 0;
    }

    return 1;
}

static MVMint32 jgb_consume_ins(MVMThreadContext *tc, JitGraphBuilder *jgb,
                                MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMint16 op = ins->info->opcode;
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
    case MVM_OP_neg_i:
    case MVM_OP_band_i:
    case MVM_OP_bor_i:
    case MVM_OP_bxor_i:
    case MVM_OP_bnot_i:
    case MVM_OP_blshift_i:
    case MVM_OP_brshift_i:
    case MVM_OP_add_n:
    case MVM_OP_sub_n:
    case MVM_OP_mul_n:
    case MVM_OP_div_n:
    case MVM_OP_neg_n:
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
    case MVM_OP_cmp_i:
        /* comparison (numbers) */
    case MVM_OP_eq_n:
    case MVM_OP_ne_n:
    case MVM_OP_ge_n:
    case MVM_OP_gt_n:
    case MVM_OP_lt_n:
    case MVM_OP_le_n:
    case MVM_OP_cmp_n:
        /* comparison (objects) */
    case MVM_OP_eqaddr:
    case MVM_OP_isconcrete:
        /* comparison (big integer) */
    case MVM_OP_eq_I:
    case MVM_OP_ne_I:
    case MVM_OP_lt_I:
    case MVM_OP_le_I:
    case MVM_OP_gt_I:
    case MVM_OP_ge_I:
        /* constants */
    case MVM_OP_const_i64_16:
    case MVM_OP_const_i64_32:
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
    case MVM_OP_getlex_no:
    case MVM_OP_bindlex:
    case MVM_OP_getwhat:
    case MVM_OP_getwho:
    case MVM_OP_getwhere:
    case MVM_OP_sp_getspeshslot:
    case MVM_OP_takedispatcher:
    case MVM_OP_setdispatcher:
    case MVM_OP_curcode:
    case MVM_OP_getcode:
    case MVM_OP_callercode:
    case MVM_OP_sp_fastcreate:
    case MVM_OP_iscont:
    case MVM_OP_decont:
    case MVM_OP_sp_namedarg_used:
    case MVM_OP_sp_findmeth:
    case MVM_OP_hllboxtype_i:
    case MVM_OP_hllboxtype_n:
    case MVM_OP_hllboxtype_s:
    case MVM_OP_null_s:
    case MVM_OP_isnull_s:
    case MVM_OP_not_i:
    case MVM_OP_isnull:
    case MVM_OP_isnonnull:
    case MVM_OP_isint:
    case MVM_OP_isnum:
    case MVM_OP_isstr:
    case MVM_OP_islist:
    case MVM_OP_ishash:
    case MVM_OP_sp_boolify_iter_arr:
    case MVM_OP_sp_boolify_iter_hash:
    case MVM_OP_objprimspec:
    case MVM_OP_takehandlerresult:
    case MVM_OP_lexoticresult:
    case MVM_OP_scwbdisable:
    case MVM_OP_scwbenable:
    case MVM_OP_assign:
    case MVM_OP_assignunchecked:
    case MVM_OP_getlexstatic_o:
    case MVM_OP_getlexperinvtype_o:
    case MVM_OP_paramnamesused:
    case MVM_OP_assertparamcheck:
    case MVM_OP_getobjsc:
        /* Profiling */
    case MVM_OP_prof_enterspesh:
    case MVM_OP_prof_enterinline:
    case MVM_OP_invokewithcapture:
        jgb_append_primitive(tc, jgb, ins);
        break;
        /* branches */
    case MVM_OP_goto:
    case MVM_OP_if_i:
    case MVM_OP_unless_i:
    case MVM_OP_if_n:
    case MVM_OP_unless_n:
    case MVM_OP_ifnonnull:
    case MVM_OP_indexat:
    case MVM_OP_indexnat:
    case MVM_OP_if_s0:
    case MVM_OP_unless_s0:
        jgb_append_branch(tc, jgb, 0, ins);
        break;
    case MVM_OP_if_o:
    case MVM_OP_unless_o: {
        /* Very special / funky branches. The function involved in
         * making this decision - namely, MVM_coerse_istrue - expects
         * to take a return register address /or/ two bytecode
         * addresses.  This is a reasonable decision with regards to
         * invocation nesting in the interpreter, but not for the
         * JIT. Hence, we will transform this into the istrue /
         * isfalse primitive combined with the if_i branch. A special
         * problem is that there really isn't any 'real' work space
         * available to store the result. Instead, we'll use the
         * args space to store and read the result */
        MVMint16 obj = ins->operands[0].reg.orig;
        /* Assign the very last register allocated */
        MVMint16 dst = jgb->sg->num_locals + jgb->sg->sf->body.cu->body.max_callsite_size - 1;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL,  { obj } },
                                 { MVM_JIT_REG_ADDR, { dst } }, // destination register (in args space)
                                 { MVM_JIT_LITERAL, { 0 } }, // true code
                                 { MVM_JIT_LITERAL, { 0 } }, // false code
                                 { MVM_JIT_LITERAL, { op == MVM_OP_unless_o } }}; // switch
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
        /* some functions */
    case MVM_OP_gethow: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_istype: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_checkarity: {
        MVMuint16 min = ins->operands[0].lit_i16;
        MVMuint16 max = ins->operands[1].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { min } },
                                 { MVM_JIT_LITERAL, { max } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_say:
    case MVM_OP_print: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } } };
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
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CU } },
                                 { MVM_JIT_LITERAL, { dep } },
                                 { MVM_JIT_LITERAL, { idx } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_die: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 str = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op),
                          3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getdynlex: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CALLER } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_binddynlex: {
        MVMint16 name = ins->operands[0].reg.orig;
        MVMint16 val  = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_REG_VAL, { val }  },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CALLER } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op),
                          4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getlexouter: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_isfalse:
    case MVM_OP_istrue: {
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL,  { obj } },
                                 { MVM_JIT_REG_ADDR, { dst } },
                                 { MVM_JIT_LITERAL, { 0 } },
                                 { MVM_JIT_LITERAL, { 0 } },
                                 { MVM_JIT_LITERAL, { op == MVM_OP_isfalse } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 6, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_capturelex: {
        MVMint16 code = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { code } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_takeclosure: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_newlexotic: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 label = get_label_for_bb(tc, jgb, ins->operands[1].ins_bb);
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { label } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_usecapture:
    case MVM_OP_savecapture: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_FRAME } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_flattenropes: {
        MVMint32 target = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_hllize: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMHLLConfig *hll_config = jgb->sg->sf->body.cu->body.hll_config;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_LITERAL_PTR, { (MVMint64)hll_config } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_clone: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* repr ops */
    case MVM_OP_unshift_i:
    case MVM_OP_unshift_n:
    case MVM_OP_unshift_s:
    case MVM_OP_unshift_o:
    case MVM_OP_push_i:
    case MVM_OP_push_n:
    case MVM_OP_push_s:
    case MVM_OP_push_o:
    case MVM_OP_shift_i:
    case MVM_OP_shift_n:
    case MVM_OP_shift_s:
    case MVM_OP_shift_o:
    case MVM_OP_pop_i:
    case MVM_OP_pop_n:
    case MVM_OP_pop_s:
    case MVM_OP_pop_o:
    case MVM_OP_deletekey:
    case MVM_OP_existskey:
    case MVM_OP_existspos:
    case MVM_OP_setelemspos:
    case MVM_OP_splice:
    case MVM_OP_atpos_i:
    case MVM_OP_atpos_n:
    case MVM_OP_atpos_s:
    case MVM_OP_atpos_o:
    case MVM_OP_atkey_i:
    case MVM_OP_atkey_n:
    case MVM_OP_atkey_s:
    case MVM_OP_atkey_o:
    case MVM_OP_bindpos_i:
    case MVM_OP_bindpos_n:
    case MVM_OP_bindpos_s:
    case MVM_OP_bindpos_o:
    case MVM_OP_bindkey_i:
    case MVM_OP_bindkey_n:
    case MVM_OP_bindkey_s:
    case MVM_OP_bindkey_o:
    case MVM_OP_getattr_i:
    case MVM_OP_getattr_n:
    case MVM_OP_getattr_s:
    case MVM_OP_getattr_o:
    case MVM_OP_getattrs_i:
    case MVM_OP_getattrs_n:
    case MVM_OP_getattrs_s:
    case MVM_OP_getattrs_o:
    case MVM_OP_bindattr_i:
    case MVM_OP_bindattr_n:
    case MVM_OP_bindattr_s:
    case MVM_OP_bindattr_o:
    case MVM_OP_bindattrs_i:
    case MVM_OP_bindattrs_n:
    case MVM_OP_bindattrs_s:
    case MVM_OP_bindattrs_o:
    case MVM_OP_elems:
        if (!jgb_consume_reprop(tc, jgb, bb, ins)) {
            MVM_jit_log(tc, "BAIL: op <%s> (devirt attempted)\n", ins->info->name);
            return 0;
        }
        break;
    case MVM_OP_iterkey_s:
    case MVM_OP_iterval:
    case MVM_OP_iter: {
        MVMint16 dst      = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { invocant } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_sp_boolify_iter: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_findmeth:
    case MVM_OP_findmeth_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint32 name = (op == MVM_OP_findmeth_s ? ins->operands[2].reg.orig :
                         ins->operands[2].lit_str_idx);
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { (op == MVM_OP_findmeth_s ? MVM_JIT_REG_VAL :
                                    MVM_JIT_STR_IDX), { name } },
                                 { MVM_JIT_REG_ADDR, { dst } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }

    case MVM_OP_multicachefind: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 cache = ins->operands[1].reg.orig;
        MVMint16 capture = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { cache } },
                                 { MVM_JIT_REG_VAL, { capture } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_multicacheadd: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 cache = ins->operands[1].reg.orig;
        MVMint16 capture = ins->operands[2].reg.orig;
        MVMint16 result = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { cache } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { result } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }

    case MVM_OP_can:
    case MVM_OP_can_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint32 name = (op == MVM_OP_can_s ? ins->operands[2].reg.orig :
                         ins->operands[2].lit_str_idx);
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { (op == MVM_OP_can_s ? MVM_JIT_REG_VAL :
                                    MVM_JIT_STR_IDX), { name } },
                                 { MVM_JIT_REG_ADDR, { dst } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }

        /* coercion */
    case MVM_OP_coerce_sn:
    case MVM_OP_coerce_ns:
    case MVM_OP_coerce_si:
    case MVM_OP_coerce_is:
    case MVM_OP_coerce_In: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[2] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        MVMJitRVMode rv_mode = ((op == MVM_OP_coerce_sn || op == MVM_OP_coerce_In) ? MVM_JIT_RV_NUM :
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
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_write_fhs: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 fho = ins->operands[1].reg.orig;
        MVMint16 str = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_LITERAL, { 0 } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_box_n:
    case MVM_OP_box_s:
    case MVM_OP_box_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 val = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { op == MVM_OP_box_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, { val } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_unbox_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_unbox_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_unbox_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* string ops */
    case MVM_OP_repeat_s:
    case MVM_OP_split:
    case MVM_OP_concat_s: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_escape:
    case MVM_OP_uc:
    case MVM_OP_lc:
    case MVM_OP_tc: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 string = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { string } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_ne_s:
    case MVM_OP_eq_s: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, MVM_OP_eq_s), 3, args,
                          MVM_JIT_RV_INT, dst);
        if (op == MVM_OP_ne_s) {
            /* append not_i to negate ne_s */
            MVMSpeshIns *not_i          = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshIns));
            not_i->info                 = MVM_op_get_op(MVM_OP_not_i);
            not_i->operands             = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMSpeshOperand) * 2);
            not_i->operands[0].reg.orig = dst;
            not_i->operands[1].reg.orig = dst;
            jgb_append_primitive(tc, jgb, not_i);
        }
        break;
    }
    case MVM_OP_eqat_s: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_ordat: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src    = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_chars:
    case MVM_OP_graphs_s:
    case MVM_OP_codes_s:
    case MVM_OP_flip: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        MVMJitRVMode rv_mode = (op == MVM_OP_flip ? MVM_JIT_RV_PTR : MVM_JIT_RV_INT);
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
    case MVM_OP_join: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 sep   = ins->operands[1].reg.orig;
        MVMint16 input = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { sep } },
                                 { MVM_JIT_REG_VAL, { input } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_substr_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 string = ins->operands[1].reg.orig;
        MVMint16 start = ins->operands[2].reg.orig;
        MVMint16 length = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { string } },
                                 { MVM_JIT_REG_VAL, { start } },
                                 { MVM_JIT_REG_VAL, { length } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_index_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 haystack = ins->operands[1].reg.orig;
        MVMint16 needle = ins->operands[2].reg.orig;
        MVMint16 start = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { haystack } },
                                 { MVM_JIT_REG_VAL, { needle } },
                                 { MVM_JIT_REG_VAL, { start } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_iscclass: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 cclass = ins->operands[1].reg.orig;
        MVMint16 str    = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { cclass } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_findcclass:
    case MVM_OP_findnotcclass: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 cclass = ins->operands[1].reg.orig;
        MVMint16 target = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMint16 count  = ins->operands[4].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { cclass } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { offset } },
                                 { MVM_JIT_REG_VAL, { count } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 5, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_nfarunalt: {
        MVMint16 nfa    = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMint16 offset = ins->operands[2].reg.orig;
        MVMint16 bstack = ins->operands[3].reg.orig;
        MVMint16 cstack = ins->operands[4].reg.orig;
        MVMint16 labels = ins->operands[5].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { nfa } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { offset } },
                                 { MVM_JIT_REG_VAL, { bstack } },
                                 { MVM_JIT_REG_VAL, { cstack } },
                                 { MVM_JIT_REG_VAL, { labels } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_nfarunproto: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 nfa     = ins->operands[1].reg.orig;
        MVMint16 target  = ins->operands[2].reg.orig;
        MVMint16 offset  = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { nfa } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_nfafromstatelist: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 states  = ins->operands[1].reg.orig;
        MVMint16 type    = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { states } },
                                 { MVM_JIT_REG_VAL, { type } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* bigint ops */
    case MVM_OP_isbig_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_cmp_I: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_add_I:
    case MVM_OP_sub_I:
    case MVM_OP_mul_I:
    case MVM_OP_div_I:
    case MVM_OP_mod_I:
    case MVM_OP_lcm_I:
    case MVM_OP_gcd_I: {
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMint16 type  = ins->operands[3].reg.orig;
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_div_In: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src_a = ins->operands[1].reg.orig;
        MVMint16 src_b = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_coerce_Is: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_LITERAL, { 10 } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_radix_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 radix = ins->operands[1].reg.orig;
        MVMint16 string = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMint16 flag = ins->operands[4].reg.orig;
        MVMint16 type = ins->operands[5].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { radix } },
                                 { MVM_JIT_REG_VAL, { string } },
                                 { MVM_JIT_REG_VAL, { offset } },
                                 { MVM_JIT_REG_VAL, { flag } },
                                 { MVM_JIT_REG_VAL, { type } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 6, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_base_I: {
        MVMint16 src  = ins->operands[1].reg.orig;
        MVMint16 base = ins->operands[2].reg.orig;
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_REG_VAL, { base } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bool_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_getcodeobj: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_sqrt_n:
    case MVM_OP_sin_n:
    case MVM_OP_cos_n:
    case MVM_OP_tan_n:
    case MVM_OP_asin_n:
    case MVM_OP_acos_n:
    case MVM_OP_atan_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src   = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_REG_VAL_F, { src } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 1, args,
                          MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_pow_n:
    case MVM_OP_atan2_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 a     = ins->operands[1].reg.orig;
        MVMint16 b     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_REG_VAL_F, { a } },
                                 { MVM_JIT_REG_VAL_F, { b } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args,
                          MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_time_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 1, args,
                          MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_randscale_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 scale = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL_F, { scale } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_isnanorinf: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL_F, { src } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_nativecallinvoke: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 restype = ins->operands[1].reg.orig;
        MVMint16 site    = ins->operands[2].reg.orig;
        MVMint16 cargs   = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { restype } },
                                 { MVM_JIT_REG_VAL, { site } },
                                 { MVM_JIT_REG_VAL, { cargs } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
        /* native references (as simple function calls for now) */
    case MVM_OP_iscont_i:
    case MVM_OP_iscont_n:
    case MVM_OP_iscont_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_assign_i:
    case MVM_OP_assign_n:
    case MVM_OP_assign_s: {
        MVMint16 target = ins->operands[0].reg.orig;
        MVMint16 value  = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { value } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_decont_i:
    case MVM_OP_decont_n:
    case MVM_OP_decont_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_ADDR, { dst } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getlexref_i:
    case MVM_OP_getlexref_n:
    case MVM_OP_getlexref_s: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMuint16 outers = ins->operands[1].lex.outers;
        MVMuint16 idx    = ins->operands[1].lex.idx;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { outers } },
                                 { MVM_JIT_LITERAL, { idx } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* profiling */
    case MVM_OP_prof_allocated: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_prof_exit: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 1, args, MVM_JIT_RV_VOID, -1);
        break;
    }
        /* special jumplist branch */
    case MVM_OP_jumplist: {
        return jgb_consume_jumplist(tc, jgb, ins);
    }
        /* returning */
    case MVM_OP_return: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { 0 } }};
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        jgb_append_branch(tc, jgb, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_return_o:
    case MVM_OP_return_s:
    case MVM_OP_return_n:
    case MVM_OP_return_i: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[3] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } },
                                 { MVM_JIT_LITERAL, { 0 } } };
        if (op == MVM_OP_return_n) {
            args[1].type = MVM_JIT_REG_VAL_F;
        }
        jgb_append_call_c(tc, jgb, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        jgb_append_branch(tc, jgb, MVM_JIT_BRANCH_EXIT, NULL);
        break;
    }
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
    case MVM_OP_sp_guardcontconc:
    case MVM_OP_sp_guardconttype:
        jgb_append_guard(tc, jgb, ins);
        break;
    case MVM_OP_prepargs: {
        return jgb_consume_invoke(tc, jgb, ins);
    }
    default: {
        /* Check if it's an extop. */
        MVMint32 emitted_extop = 0;
        if (ins->info->opcode == (MVMuint16)-1) {
            MVMExtOpRecord *extops     = jgb->sg->sf->body.cu->body.extops;
            MVMuint16       num_extops = jgb->sg->sf->body.cu->body.num_extops;
            MVMuint16       i;
            for (i = 0; i < num_extops; i++) {
                if (extops[i].info == ins->info) {
                    MVMuint16 *fake_regs;
                    if (!extops[i].no_jit && (fake_regs = try_fake_extop_regs(tc, ins))) {
                        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  { MVM_JIT_INTERP_TC } },
                                                 { MVM_JIT_LITERAL_PTR, { (MVMint64)fake_regs } }};
                        jgb_append_call_c(tc, jgb, extops[i].func, 2, args, MVM_JIT_RV_VOID, -1);
                        if (ins->info->jittivity & MVM_JIT_INFO_INVOKISH)
                            jgb_append_control(tc, jgb, ins, MVM_JIT_CONTROL_INVOKISH);
                        MVM_jit_log(tc, "append extop: <%s>\n", ins->info->name);
                        emitted_extop = 1;
                    }
                    break;
                }
            }
        }
        if (!emitted_extop) {
            MVM_jit_log(tc, "BAIL: op <%s>\n", ins->info->name);
            return 0;
        }
    }
    }
    return 1;
}

static MVMint32 jgb_consume_bb(MVMThreadContext *tc, JitGraphBuilder *jgb,
                               MVMSpeshBB *bb) {
    MVMint32 label = get_label_for_bb(tc, jgb, bb);
    jgb->cur_bb = bb;
    jgb_append_label(tc, jgb, label);
    jgb->cur_ins = bb->first_ins;
    while (jgb->cur_ins) {
        jgb_before_ins(tc, jgb, jgb->cur_bb, jgb->cur_ins);
        if(!jgb_consume_ins(tc, jgb, jgb->cur_bb, jgb->cur_ins))
            return 0;
        jgb_after_ins(tc, jgb, jgb->cur_bb, jgb->cur_ins);
        jgb->cur_ins = jgb->cur_ins->next;
    }
    return 1;
}

static MVMJitGraph *jgb_build(MVMThreadContext *tc, JitGraphBuilder *jgb) {
    MVMint32 i;
    MVMJitGraph * jg       = MVM_spesh_alloc(tc, jgb->sg, sizeof(MVMJitGraph));
    jg->sg                 = jgb->sg;
    jg->first_node         = jgb->first_node;
    jg->last_node          = jgb->last_node;
    /* find the last assigned label */
    for (i = 0; i < jgb->num_labels; i++)
        if (jgb->labeleds[i] == NULL)
            break;
    jg->num_labels         = i;
    jg->num_bbs            = jgb->num_bbs;
    jg->bb_labels          = jgb->bb_labels;
    jg->num_deopts         = jgb->num_deopts;
    jg->deopts             = jgb->deopts;
    jg->num_inlines       = jgb->num_inlines;
    jg->inlines           = jgb->inlines;
    jg->num_handlers      = jgb->num_handlers;
    jg->handlers          = jgb->handlers;
    return jg;
}

MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    JitGraphBuilder jgb;
    if (!MVM_jit_support()) {
        return NULL;
    }

    if (tc->instance->jit_log_fh) {
        char *cuuid = MVM_string_utf8_encode_C_string(tc, sg->sf->body.cuuid);
        char *name  = MVM_string_utf8_encode_C_string(tc, sg->sf->body.name);
        MVM_jit_log(tc, "Constructing JIT graph (cuuid: %s, name: '%s')\n",
                    cuuid, name);
        MVM_free(cuuid);
        MVM_free(name);
    }

    jgb.sg = sg;
    /* ignore first BB, which always contains a NOP */
    jgb.cur_bb = sg->entry->linear_next;
    jgb.cur_ins = jgb.cur_bb->first_ins;
    jgb.first_node = jgb.last_node = NULL;
    /* Total (expected) number of labels. May grow if there are more than 4
     * deopt labels (OSR deopt labels or deopt_all labels). */
    jgb.num_labels = sg->num_bbs + (sg->num_handlers * 3) + 4;
    /* The objects that are labeled (spesh ins or spesh bbs). May grow */
    jgb.labeleds   = MVM_spesh_alloc(tc, sg, sizeof(void*) * jgb.num_labels);
    /* bb labels are indexed by bb index (much wow) */
    jgb.num_bbs    = sg->num_bbs;
    jgb.bb_labels  = MVM_spesh_alloc(tc, sg, sizeof(MVMint32) * sg->num_bbs);
    /* deopt points may grow */
    jgb.num_deopts   = 0;
    jgb.alloc_deopts = 2;
    jgb.deopts       = MVM_spesh_alloc(tc, sg, sizeof(MVMJitDeopt) * 2);
    /* jit handlers are indexed by.. handler index (also much wow) */
    jgb.num_handlers = sg->num_handlers;
    jgb.handlers     = sg->num_handlers ? MVM_spesh_alloc(tc, sg, sizeof(MVMJitHandler) * sg->num_handlers) : NULL;
    /* guess what inlines are indexed by */
    jgb.num_inlines  = sg->num_inlines;
    jgb.inlines      = sg->num_inlines ? MVM_spesh_alloc(tc, sg, sizeof(MVMJitInline) * sg->num_inlines) : NULL;
    /* loop over basic blocks, adding one after the other */
    while (jgb.cur_bb) {
        if (!jgb_consume_bb(tc, &jgb, jgb.cur_bb))
            return NULL;
        jgb.cur_bb = jgb.cur_bb->linear_next;
    }
    /* Check if we've added a instruction at all */
    if (!jgb.first_node)
        return NULL;
    /* append the end-of-graph label */
    jgb_append_label(tc, &jgb, get_label_for_graph(tc, &jgb, sg));
    return jgb_build(tc, &jgb);
}
