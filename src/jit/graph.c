#include "moar.h"

#include "platform/sys.h"
#include "platform/time.h"


static void jg_append_node(MVMJitGraph *jg, MVMJitNode *node) {
    if (jg->last_node) {
        jg->last_node->next = node;
        jg->last_node = node;
    } else {
        jg->first_node = node;
        jg->last_node = node;
    }
    node->next = NULL;
}

static void jg_append_deopt_check(MVMThreadContext *tc, MVMJitGraph *jg) {
    MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_DEOPT_CHECK;
    jg_append_node(jg, node);
}

static void jg_append_primitive(MVMThreadContext *tc, MVMJitGraph *jg,
                                MVMSpeshIns * ins) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_PRIMITIVE;
    node->u.prim.ins = ins;
    jg_append_node(jg, node);
}

static void jg_append_call_c(MVMThreadContext *tc, MVMJitGraph *jg,
                              void * func_ptr, MVMint16 num_args,
                              MVMJitCallArg *call_args,
                              MVMJitRVMode rv_mode, MVMint16 rv_idx) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    size_t args_size =  num_args * sizeof(MVMJitCallArg);
    node->type             = MVM_JIT_NODE_CALL_C;
    node->u.call.func_ptr  = func_ptr;
    node->u.call.num_args  = num_args;
    /* Call argument array is typically stack allocated,
     * so they need to be copied */
    if (num_args > 0) {
        node->u.call.args      = MVM_spesh_alloc(tc, jg->sg, args_size);
        memcpy(node->u.call.args, call_args, args_size);
    }
    else {
        node->u.call.args = NULL;
    }
    node->u.call.rv_mode   = rv_mode;
    node->u.call.rv_idx    = rv_idx;
    jg_append_node(jg, node);
}


static void add_deopt_idx(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 label_name, MVMint32 deopt_idx) {
    MVMJitDeopt deopt;
    deopt.label = label_name;
    deopt.idx   = deopt_idx;
    MVM_VECTOR_PUSH(jg->deopts, deopt);
}


static void jg_append_branch(MVMThreadContext *tc, MVMJitGraph *jg,
                              MVMint32 name, MVMSpeshIns *ins) {
    MVMJitNode * node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
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
        node->u.branch.dest = MVM_jit_label_before_bb(tc, jg, bb);
    }
    jg_append_node(jg, node);
}

static void jg_append_label(MVMThreadContext *tc, MVMJitGraph *jg, MVMint32 name) {
    MVMJitNode *node;
    /* does this label already exist? */
    MVM_VECTOR_ENSURE_SIZE(jg->label_nodes, name);
    if (jg->label_nodes[name] != NULL)
        return;

    node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_LABEL;
    node->u.label.name = name;
    jg_append_node(jg, node);

    jg->label_nodes[name] = node;
}

static void * op_to_func(MVMThreadContext *tc, MVMint16 opcode) {
    switch(opcode) {
    case MVM_OP_checkarity: return MVM_args_checkarity;
    case MVM_OP_say: return MVM_string_say;
    case MVM_OP_print: return MVM_string_print;
    case MVM_OP_isnull: return MVM_is_null;
    case MVM_OP_capturelex: return MVM_frame_capturelex;
    case MVM_OP_captureinnerlex: return MVM_frame_capture_inner;
    case MVM_OP_takeclosure: return MVM_frame_takeclosure;
    case MVM_OP_usecapture: return MVM_args_use_capture;
    case MVM_OP_savecapture: return MVM_args_save_capture;
    case MVM_OP_captureposelems: return MVM_capture_num_pos_args;
    case MVM_OP_captureposarg: return MVM_capture_arg_pos_o;
    case MVM_OP_captureposarg_i: return MVM_capture_arg_pos_i;
    case MVM_OP_captureposarg_n: return MVM_capture_arg_pos_n;
    case MVM_OP_captureposarg_s: return MVM_capture_arg_pos_s;
    case MVM_OP_captureposprimspec: return MVM_capture_arg_pos_primspec;
    case MVM_OP_captureexistsnamed: return MVM_capture_has_named_arg;
    case MVM_OP_capturehasnameds: return MVM_capture_has_nameds;
    case MVM_OP_return: return MVM_args_assert_void_return_ok;
    case MVM_OP_return_i: return MVM_args_set_result_int;
    case MVM_OP_return_u: return MVM_args_set_result_uint;
    case MVM_OP_return_s: return MVM_args_set_result_str;
    case MVM_OP_return_o: return MVM_args_set_result_obj;
    case MVM_OP_return_n: return MVM_args_set_result_num;
    case MVM_OP_coerce_is: return MVM_coerce_i_s;
    case MVM_OP_coerce_us: return MVM_coerce_u_s;
    case MVM_OP_coerce_ns: return MVM_coerce_n_s;
    case MVM_OP_coerce_si: return MVM_coerce_s_i;
    case MVM_OP_coerce_su: return MVM_coerce_s_u;
    case MVM_OP_coerce_sn: return MVM_coerce_s_n;
    case MVM_OP_coerce_In: return MVM_bigint_to_num;
    case MVM_OP_coerce_nI: return MVM_bigint_from_num;
    case MVM_OP_coerce_sI: return MVM_coerce_sI;
    case MVM_OP_coerce_II: return MVM_bigint_from_bigint;
    case MVM_OP_iterkey_s: return MVM_iterkey_s;
    case MVM_OP_iter: return MVM_iter;
    case MVM_OP_iterval: return MVM_iterval;
    case MVM_OP_die: return MVM_exception_die;
    case MVM_OP_throwdyn:
    case MVM_OP_throwlex:
    case MVM_OP_throwlexotic:
    case MVM_OP_rethrow: return MVM_exception_throwobj;
    case MVM_OP_throwcatdyn:
    case MVM_OP_throwcatlex:
    case MVM_OP_throwcatlexotic: return MVM_exception_throwcat;
    case MVM_OP_throwpayloadlex: case MVM_OP_throwpayloadlexcaller: return MVM_exception_throwpayload;
    case MVM_OP_bindexpayload: return MVM_bind_exception_payload;
    case MVM_OP_getexpayload: return MVM_get_exception_payload;
    case MVM_OP_bindexmessage: return MVM_bind_exception_message;
    case MVM_OP_getexmessage: return MVM_get_exception_message;
    case MVM_OP_resume: return MVM_exception_resume;
    case MVM_OP_continuationreset: return MVM_continuation_reset;
    case MVM_OP_continuationcontrol: return MVM_continuation_control;
    case MVM_OP_continuationinvoke: return MVM_continuation_invoke;
    case MVM_OP_gethow: return MVM_6model_get_how_obj;
    case MVM_OP_box_i: return MVM_repr_box_int;
    case MVM_OP_box_u: return MVM_repr_box_uint;
    case MVM_OP_box_s: return MVM_repr_box_str;
    case MVM_OP_box_n: return MVM_repr_box_num;
    case MVM_OP_unbox_i: return MVM_repr_get_int;
    case MVM_OP_unbox_u: return MVM_repr_get_uint;
    case MVM_OP_unbox_s: return MVM_repr_get_str;
    case MVM_OP_unbox_n: return MVM_repr_get_num;
    case MVM_OP_isint: case MVM_OP_isnum: case MVM_OP_isstr: /* continued */
    case MVM_OP_islist: case MVM_OP_ishash: return MVM_repr_compare_repr_id;
    case MVM_OP_iscoderef: return MVM_code_iscode;
    case MVM_OP_wval: case MVM_OP_wval_wide: return MVM_sc_get_sc_object;
    case MVM_OP_scgetobjidx: return MVM_sc_find_object_idx_jit;
    case MVM_OP_getdynlex: return MVM_frame_getdynlex;
    case MVM_OP_binddynlex: return MVM_frame_binddynlex;
    case MVM_OP_getlexouter: return MVM_frame_find_lexical_by_name_outer;
    case MVM_OP_getlexcaller: return MVM_frame_find_lexical_by_name_rel_caller;
    case MVM_OP_push_i: return MVM_repr_push_i;
    case MVM_OP_push_n: return MVM_repr_push_n;
    case MVM_OP_push_s: return MVM_repr_push_s;
    case MVM_OP_push_o: return MVM_repr_push_o;
    case MVM_OP_unshift_i: return MVM_repr_unshift_i;
    case MVM_OP_unshift_n: return MVM_repr_unshift_n;
    case MVM_OP_unshift_s: return MVM_repr_unshift_s;
    case MVM_OP_unshift_o: return MVM_repr_unshift_o;
    case MVM_OP_pop_i: return MVM_repr_pop_i;
    case MVM_OP_pop_n: return MVM_repr_pop_n;
    case MVM_OP_pop_s: return MVM_repr_pop_s;
    case MVM_OP_pop_o: return MVM_repr_pop_o;
    case MVM_OP_shift_i: return MVM_repr_shift_i;
    case MVM_OP_shift_n: return MVM_repr_shift_n;
    case MVM_OP_shift_s: return MVM_repr_shift_s;
    case MVM_OP_shift_o: return MVM_repr_shift_o;
    case MVM_OP_setelemspos: return MVM_repr_pos_set_elems;
    case MVM_OP_slice:  return MVM_repr_pos_slice;
    case MVM_OP_splice: return MVM_repr_pos_splice;

    case MVM_OP_existskey: return MVM_repr_exists_key;
    case MVM_OP_deletekey: return MVM_repr_delete_key;

    case MVM_OP_atpos_i: return MVM_repr_at_pos_i;
    case MVM_OP_atpos_u: return MVM_repr_at_pos_u;
    case MVM_OP_atpos_n: return MVM_repr_at_pos_n;
    case MVM_OP_atpos_s: return MVM_repr_at_pos_s;
    case MVM_OP_atpos_o: return MVM_repr_at_pos_o;

    case MVM_OP_existspos: return MVM_repr_exists_pos;

    case MVM_OP_atkey_i: return MVM_repr_at_key_i;
    case MVM_OP_atkey_u: return MVM_repr_at_key_u;
    case MVM_OP_atkey_n: return MVM_repr_at_key_n;
    case MVM_OP_atkey_s: return MVM_repr_at_key_s;
    case MVM_OP_atkey_o: return MVM_repr_at_key_o;

    case MVM_OP_bindpos_i: return MVM_repr_bind_pos_i;
    case MVM_OP_bindpos_u: return MVM_repr_bind_pos_u;
    case MVM_OP_bindpos_n: return MVM_repr_bind_pos_n;
    case MVM_OP_bindpos_s: return MVM_repr_bind_pos_s;
    case MVM_OP_bindpos_o: return MVM_repr_bind_pos_o;

    case MVM_OP_writeint:  return MVM_repr_write_buf;
    case MVM_OP_writeuint: return MVM_repr_write_buf;

    case MVM_OP_readint:  return MVM_repr_read_buf;
    case MVM_OP_readuint: return MVM_repr_read_buf;

    case MVM_OP_bindkey_i: return MVM_repr_bind_key_i;
    case MVM_OP_bindkey_n: return MVM_repr_bind_key_n;
    case MVM_OP_bindkey_s: return MVM_repr_bind_key_s;
    case MVM_OP_bindkey_o: return MVM_repr_bind_key_o;

    case MVM_OP_getattr_s: return MVM_repr_get_attr_s;
    case MVM_OP_getattr_n: return MVM_repr_get_attr_n;
    case MVM_OP_getattr_i: return MVM_repr_get_attr_i;
    case MVM_OP_getattr_o: return MVM_repr_get_attr_o;
    case MVM_OP_getattr_u: return MVM_repr_get_attr_u;

    case MVM_OP_getattrs_s: return MVM_repr_get_attr_s;
    case MVM_OP_getattrs_n: return MVM_repr_get_attr_n;
    case MVM_OP_getattrs_i: return MVM_repr_get_attr_i;
    case MVM_OP_getattrs_o: return MVM_repr_get_attr_o;
    case MVM_OP_getattrs_u: return MVM_repr_get_attr_u;

    case MVM_OP_attrinited: return MVM_repr_attribute_inited;

    case MVM_OP_bindattr_i: case MVM_OP_bindattr_n: case MVM_OP_bindattr_s: case MVM_OP_bindattr_o: case MVM_OP_bindattr_u: return MVM_repr_bind_attr_inso;
    case MVM_OP_bindattrs_i: case MVM_OP_bindattrs_n: case MVM_OP_bindattrs_s: case MVM_OP_bindattrs_o: case MVM_OP_bindattrs_u: return MVM_repr_bind_attr_inso;

    case MVM_OP_hintfor: return MVM_repr_hint_for;

    case MVM_OP_gt_s: case MVM_OP_ge_s: case MVM_OP_lt_s: case MVM_OP_le_s: case MVM_OP_cmp_s: return MVM_string_compare;

    case MVM_OP_queuepoll: return MVM_concblockingqueue_jit_poll;

    case MVM_OP_open_dir: return MVM_dir_open;
    case MVM_OP_read_dir: return MVM_dir_read;
    case MVM_OP_close_dir: return MVM_dir_close;
    case MVM_OP_open_fh: return MVM_file_open_fh;
    case MVM_OP_close_fh: return MVM_io_close;
    case MVM_OP_eof_fh: return MVM_io_eof;
    case MVM_OP_istty_fh: return MVM_io_is_tty;
    case MVM_OP_fileno_fh: return MVM_io_fileno;
    case MVM_OP_write_fhb: return MVM_io_write_bytes;
    case MVM_OP_read_fhb: return MVM_io_read_bytes;
    case MVM_OP_seek_fh: return MVM_io_seek;

    case MVM_OP_encode: return MVM_string_encode_to_buf;
    case MVM_OP_decoderaddbytes: return MVM_decoder_add_bytes;
    case MVM_OP_decodertakeline: return MVM_decoder_take_line;
    case MVM_OP_decodertakeallchars: return MVM_decoder_take_all_chars;
    case MVM_OP_decodertakebytes: return MVM_decoder_take_bytes;
    case MVM_OP_decoderempty: return MVM_decoder_empty;

    case MVM_OP_elems: return MVM_repr_elems;
    case MVM_OP_concat_s: return MVM_string_concatenate;
    case MVM_OP_repeat_s: return MVM_string_repeat;
    case MVM_OP_flip: return MVM_string_flip;
    case MVM_OP_split: return MVM_string_split;
    case MVM_OP_escape: return MVM_string_escape;
    case MVM_OP_uc: return MVM_string_uc;
    case MVM_OP_tc: return MVM_string_tc;
    case MVM_OP_lc: return MVM_string_lc;
    case MVM_OP_fc: return MVM_string_fc;
    case MVM_OP_eq_s: return MVM_string_equal;
    case MVM_OP_eqat_s: return MVM_string_equal_at;
    case MVM_OP_eqatic_s: return MVM_string_equal_at_ignore_case;
    case MVM_OP_eqatim_s: return MVM_string_equal_at_ignore_mark;
    case MVM_OP_eqaticim_s: return MVM_string_equal_at_ignore_case_ignore_mark;
    case MVM_OP_chars: return MVM_string_graphs;
    case MVM_OP_chr: return MVM_string_chr;
    case MVM_OP_codes_s: return MVM_string_codes;
    case MVM_OP_getcp_s: return MVM_string_get_grapheme_at;
    case MVM_OP_index_s: return MVM_string_index;
    case MVM_OP_substr_s: return MVM_string_substring;
    case MVM_OP_join: return MVM_string_join;
    case MVM_OP_replace: return MVM_string_replace;
    case MVM_OP_iscclass: return MVM_string_is_cclass;
    case MVM_OP_findcclass: return MVM_string_find_cclass;
    case MVM_OP_findnotcclass: return MVM_string_find_not_cclass;
    case MVM_OP_nfarunalt: return MVM_nfa_run_alt;
    case MVM_OP_nfarunproto: return MVM_nfa_run_proto;
    case MVM_OP_nfafromstatelist: return MVM_nfa_from_statelist;
    case MVM_OP_gethllsym: return MVM_hll_sym_get;
    case MVM_OP_istrue_s: case MVM_OP_isfalse_s: return MVM_coerce_istrue_s;
    case MVM_OP_clone: return MVM_repr_clone;
    case MVM_OP_create: return MVM_repr_alloc_init;
    case MVM_OP_getcodeobj: return MVM_frame_get_code_object;
    case MVM_OP_isbig_I: return MVM_bigint_is_big;
    case MVM_OP_cmp_I: return MVM_bigint_cmp;
    case MVM_OP_add_I: return MVM_bigint_add;
    case MVM_OP_sub_I: return MVM_bigint_sub;
    case MVM_OP_mul_I: return MVM_bigint_mul;
    case MVM_OP_div_I: return MVM_bigint_div;
    case MVM_OP_neg_I: return MVM_bigint_neg;
    case MVM_OP_abs_I: return MVM_bigint_abs;
    case MVM_OP_bor_I: return MVM_bigint_or;
    case MVM_OP_band_I: return MVM_bigint_and;
    case MVM_OP_bxor_I: return MVM_bigint_xor;
    case MVM_OP_mod_I: return MVM_bigint_mod;
    case MVM_OP_lcm_I: return MVM_bigint_lcm;
    case MVM_OP_gcd_I: return MVM_bigint_gcd;
    case MVM_OP_bool_I: return MVM_bigint_bool;
    case MVM_OP_isprime_I: return MVM_bigint_is_prime;
    case MVM_OP_brshift_I: return MVM_bigint_shr;
    case MVM_OP_blshift_I: return MVM_bigint_shl;
    case MVM_OP_bnot_I: return MVM_bigint_not;
    case MVM_OP_div_In: return MVM_bigint_div_num;
    case MVM_OP_coerce_Is: case MVM_OP_base_I: return MVM_bigint_to_str;
    case MVM_OP_radix: return MVM_radix;
    case MVM_OP_radix_I: return MVM_bigint_radix;
    case MVM_OP_sqrt_n: return sqrt;
#ifndef MVM_HAS_SUBSTANDARD_LOG
    case MVM_OP_log_n: return log;
#endif
    case MVM_OP_exp_n: return exp;
    case MVM_OP_sin_n: return sin;
    case MVM_OP_cos_n: return cos;
    case MVM_OP_tan_n: return tan;
#ifndef MVM_HAS_SUBSTANDARD_ASIN
    case MVM_OP_asin_n: return asin;
#endif
#ifndef MVM_HAS_SUBSTANDARD_ACOS
    case MVM_OP_acos_n: return acos;
#endif
    case MVM_OP_atan_n: return atan;
    case MVM_OP_atan2_n: return atan2;
    case MVM_OP_ceil_n: return ceil;
    case MVM_OP_floor_n: return floor;
    case MVM_OP_pow_I: return MVM_bigint_pow;
    case MVM_OP_rand_I: return MVM_bigint_rand;
    case MVM_OP_abs_n: return fabs;
#ifndef MVM_HAS_SUBSTANDARD_POW
    case MVM_OP_pow_n: return pow;
#endif
    case MVM_OP_time: return MVM_proc_time;
    case MVM_OP_randscale_n: return MVM_proc_randscale_n;
    case MVM_OP_isnanorinf: return MVM_num_isnanorinf;
    case MVM_OP_nativecallcast: return MVM_nativecall_cast;
    case MVM_OP_nativecallinvoke: return MVM_nativecall_invoke;
    case MVM_OP_typeparameterized: return MVM_6model_parametric_type_parameterized;
    case MVM_OP_typeparameters: return MVM_6model_parametric_type_parameters;
    case MVM_OP_typeparameterat: return MVM_6model_parametric_type_parameter_at;
    case MVM_OP_parameterizetype: return MVM_6model_parametric_parameterize;
    case MVM_OP_setparameterizer: return MVM_6model_parametric_setup;
    case MVM_OP_objectid: return MVM_gc_object_id;
    case MVM_OP_iscont_i: return MVM_6model_container_iscont_i;
    case MVM_OP_iscont_n: return MVM_6model_container_iscont_n;
    case MVM_OP_iscont_s: return MVM_6model_container_iscont_s;
    case MVM_OP_iscont_u: return MVM_6model_container_iscont_u;
    case MVM_OP_isrwcont: return MVM_6model_container_iscont_rw;
    case MVM_OP_assign_i: return MVM_6model_container_assign_i;
    case MVM_OP_assign_n: return MVM_6model_container_assign_n;
    case MVM_OP_assign_s: return MVM_6model_container_assign_s;
    case MVM_OP_assign_u: return MVM_6model_container_assign_u;
    case MVM_OP_decont_i: return MVM_6model_container_decont_i;
    case MVM_OP_decont_n: return MVM_6model_container_decont_n;
    case MVM_OP_decont_s: return MVM_6model_container_decont_s;
    case MVM_OP_decont_u: return MVM_6model_container_decont_u;
    case MVM_OP_getrusage: return MVM_proc_getrusage;
    case MVM_OP_cpucores: return MVM_platform_cpu_count;
    case MVM_OP_freemem: return MVM_platform_free_memory;
    case MVM_OP_totalmem: return MVM_platform_total_memory;
    case MVM_OP_getsignals: return MVM_io_get_signals;
    case MVM_OP_sleep: return MVM_platform_sleep;
    case MVM_OP_getlexref_i32: case MVM_OP_getlexref_i16: case MVM_OP_getlexref_i8: case MVM_OP_getlexref_i: return MVM_nativeref_lex_i;
    case MVM_OP_getlexref_u32: case MVM_OP_getlexref_u16: case MVM_OP_getlexref_u8: case MVM_OP_getlexref_u: return MVM_nativeref_lex_u;
    case MVM_OP_getlexref_n32: case MVM_OP_getlexref_n: return MVM_nativeref_lex_n;
    case MVM_OP_getlexref_s: return MVM_nativeref_lex_s;
    case MVM_OP_getattrref_i: return MVM_nativeref_attr_i;
    case MVM_OP_getattrref_u: return MVM_nativeref_attr_u;
    case MVM_OP_getattrref_n: return MVM_nativeref_attr_n;
    case MVM_OP_getattrref_s: return MVM_nativeref_attr_s;
    case MVM_OP_getattrsref_i: return MVM_nativeref_attr_i;
    case MVM_OP_getattrsref_n: return MVM_nativeref_attr_n;
    case MVM_OP_getattrsref_s: return MVM_nativeref_attr_s;
    case MVM_OP_atposref_i: return MVM_nativeref_pos_i;
    case MVM_OP_atposref_u: return MVM_nativeref_pos_u;
    case MVM_OP_atposref_n: return MVM_nativeref_pos_n;
    case MVM_OP_atposref_s: return MVM_nativeref_pos_s;
    case MVM_OP_indexingoptimized: return MVM_string_indexing_optimized;
    case MVM_OP_sp_boolify_iter: return MVM_iter_istrue;
    case MVM_OP_prof_allocated: return MVM_profile_log_allocated;
    case MVM_OP_prof_exit: return MVM_profile_log_exit;

    case MVM_OP_cas_o: return MVM_6model_container_cas;
    case MVM_OP_cas_i: return MVM_6model_container_cas_i;
    case MVM_OP_atomicinc_i: return MVM_6model_container_atomic_inc;
    case MVM_OP_atomicdec_i: return MVM_6model_container_atomic_dec;
    case MVM_OP_atomicadd_i: return MVM_6model_container_atomic_add;
    case MVM_OP_atomicload_o: return MVM_6model_container_atomic_load;
    case MVM_OP_atomicload_i: return MVM_6model_container_atomic_load_i;
    case MVM_OP_atomicstore_o: return MVM_6model_container_atomic_store;
    case MVM_OP_atomicstore_i: return MVM_6model_container_atomic_store_i;
    case MVM_OP_lock: return MVM_reentrantmutex_lock_checked;
    case MVM_OP_unlock: return MVM_reentrantmutex_unlock_checked;
    case MVM_OP_getexcategory: return MVM_get_exception_category;
    case MVM_OP_bindexcategory: return MVM_bind_exception_category;
    case MVM_OP_exreturnafterunwind: return MVM_exception_returnafterunwind;

    case MVM_OP_backtrace: return MVM_exception_backtrace;
    case MVM_OP_backtracestrings: return MVM_exception_backtrace_strings;
    case MVM_OP_breakpoint: return MVM_debugserver_breakpoint_check;
    case MVM_OP_sp_getstringfrom: return MVM_cu_string;
    case MVM_OP_encoderepconf: return MVM_string_encode_to_buf_config;
    case MVM_OP_decodeconf: return MVM_string_decode_from_buf_config;
    case MVM_OP_decoderepconf: return MVM_string_decode_from_buf_config;
    case MVM_OP_strfromname: return MVM_unicode_string_from_name;
    case MVM_OP_strfromcodes: return MVM_unicode_codepoints_to_nfg_string;
    case MVM_OP_callercode: return MVM_frame_caller_code;
    case MVM_OP_stat: return MVM_file_stat;
    case MVM_OP_lstat: return MVM_file_stat;

    case MVM_OP_getuniprop_int: return MVM_unicode_codepoint_get_property_int;
    case MVM_OP_getuniprop_bool: return MVM_unicode_codepoint_get_property_bool;
    case MVM_OP_getuniprop_str: return MVM_unicode_codepoint_get_property_str;

    case MVM_OP_loadlib: return MVM_dll_load;

    case MVM_OP_sha1: return MVM_sha1;

    case MVM_OP_loadext: return MVM_ext_load;

    case MVM_OP_settypefinalize: return MVM_gc_finalize_set;

    case MVM_OP_usecompileehllconfig: return MVM_hll_enter_compilee_mode;
    case MVM_OP_usecompilerhllconfig: return MVM_hll_leave_compilee_mode;

    case MVM_OP_getcurhllsym: return MVM_hll_sym_get;

    case MVM_OP_scsetobj: return MVM_sc_set_object_op;
    case MVM_OP_scsetcode: return MVM_sc_set_code_op;

    case MVM_OP_setdebugtypename: return MVM_6model_set_debug_name;

    default:
        MVM_oops(tc, "JIT: No function for op %d in op_to_func (%s)", opcode, MVM_op_get_op(opcode)->name);
    }
}

static void jg_append_guard(MVMThreadContext *tc, MVMJitGraph *jg,
                             MVMSpeshIns *ins, MVMuint32 target_operand) {
    MVMJitNode   *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    MVMint32 deopt_idx;
    node->type = MVM_JIT_NODE_GUARD;
    node->u.guard.ins = ins;
    switch (ins->info->opcode) {
    case MVM_OP_sp_guard:
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
    case MVM_OP_sp_guardobj:
    case MVM_OP_sp_guardnotobj:
    case MVM_OP_sp_guardhll:
    case MVM_OP_sp_rebless:
        deopt_idx = ins->operands[3].lit_ui32;
        break;
    case MVM_OP_sp_guardsf:
    case MVM_OP_sp_guardsfouter:
    case MVM_OP_sp_guardjustconc:
    case MVM_OP_sp_guardjusttype:
    case MVM_OP_sp_guardnonzero:
        deopt_idx = ins->operands[2].lit_ui32;
        break;
    default:
        abort();
        break;
    }
    node->u.guard.deopt_idx = deopt_idx;
    jg_append_node(jg, node);
}

static void jg_append_control(MVMThreadContext *tc, MVMJitGraph *jg,
                              MVMSpeshIns *ins, MVMJitControlType ctrl) {
    MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_CONTROL;
    node->u.control.ins  = ins;
    node->u.control.type = ctrl;
    jg_append_node(jg, node);
}

static MVMint32 consume_jumplist(MVMThreadContext *tc, MVMJitGraph *jg,
                                 MVMSpeshIterator *iter, MVMSpeshIns *ins) {
    MVMint64 num_labels  = ins->operands[0].lit_i64;
    MVMint16 idx_reg     = ins->operands[1].reg.orig;
    MVMint32 *in_labels  = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMint32) * num_labels);
    MVMint32 *out_labels = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMint32) * num_labels);
    MVMSpeshBB *bb       = iter->bb;
    MVMJitNode *node;
    MVMint64 i;
    for (i = 0; i < num_labels; i++) {
        bb = bb->linear_next; /* take the next basic block */
        if (!bb || bb->first_ins != bb->last_ins) return 0; /*  which must exist */
        ins = bb->first_ins;  /*  and it's first and only entry */
        if (ins->info->opcode != MVM_OP_goto)  /* which must be a goto */
            return 0;
        in_labels[i]  = MVM_jit_label_before_bb(tc, jg, bb);
        out_labels[i] = MVM_jit_label_before_bb(tc, jg, ins->operands[0].ins_bb);
    }
    /* build the node */
    node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    node->type = MVM_JIT_NODE_JUMPLIST;
    node->u.jumplist.num_labels = num_labels;
    node->u.jumplist.reg = idx_reg;
    node->u.jumplist.in_labels = in_labels;
    node->u.jumplist.out_labels = out_labels;
    jg_append_node(jg, node);
    /* set iterator bb and ins to the end of our jumplist */
    iter->bb = bb;
    iter->ins = ins;
    return 1;
}

static MVMString* wrap_MVM_cu_string(MVMThreadContext *tc, MVMCompUnit *cu, MVMuint32 idx) {
    return MVM_cu_string(tc, cu, idx);
}

static MVMint32 jg_add_data_node(MVMThreadContext *tc, MVMJitGraph *jg, void *data, size_t size) {
    MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
    MVMint32 label   = MVM_jit_label_for_obj(tc, jg, data);
    node->type         = MVM_JIT_NODE_DATA;
    node->u.data.data  = data;
    node->u.data.size  = size;
    node->u.data.label = label;
    jg_append_node(jg, node);
    return label;
}

static MVMuint16 * try_fake_extop_regs(MVMThreadContext *tc, MVMSpeshGraph *sg, MVMSpeshIns *ins, size_t *bufsize) {
    MVMuint16 *regs = MVM_spesh_alloc(tc, sg, (*bufsize = (ins->info->num_operands * sizeof(MVMuint16))));
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

static void before_ins(MVMThreadContext *tc, MVMJitGraph *jg,
                       MVMSpeshIterator *iter, MVMSpeshIns  *ins) {
    MVMSpeshBB   *bb = iter->bb;
    MVMSpeshAnn *ann = ins->annotations;

    MVMint32 has_label = 0, label;
    /* Search annotations for stuff that may need a label. */
    while (ann) {
        switch(ann->type) {
        case MVM_SPESH_ANN_DEOPT_OSR: {
            /* get label before our instruction */
            label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            add_deopt_idx(tc, jg, label, ann->data.deopt_idx);
            has_label = 1;
            break;
        }
        case MVM_SPESH_ANN_FH_START: {
            label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            jg->handlers[ann->data.frame_handler_index].start_label = label;
            has_label = 1;
            /* Load the current position into the jit entry label, so that
             * when throwing we'll know which handler to use */
            break;
        }
        case MVM_SPESH_ANN_FH_END: {
            label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            jg->handlers[ann->data.frame_handler_index].end_label = label;
            /* Same as above. Note that the dynamic label control
             * actually loads a position a few bytes away from the
             * label appended above. This is in this case intentional
             * because the frame handler end is exclusive; once it is
             * passed we should not use the same handler again.  If we
             * loaded the exact same position, we would not be able to
             * distinguish between the end of the basic block to which
             * the handler applies and the start of the basic block to
             * which it doesn't. */
            has_label = 1;
            break;
        }
        case MVM_SPESH_ANN_FH_GOTO: {
            label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            jg->handlers[ann->data.frame_handler_index].goto_label = label;
            has_label = 1;
            break;
        }
        case MVM_SPESH_ANN_INLINE_START: {
            label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            jg->inlines[ann->data.inline_idx].start_label = label;
            has_label = 1;
            break;
        }
        } /* switch */
        ann = ann->next;
    }

    if (has_label) {
        jg_append_label(tc, jg, label);
    }
}

static void after_ins(MVMThreadContext *tc, MVMJitGraph *jg,
                      MVMSpeshIterator *iter, MVMSpeshIns *ins) {
    MVMSpeshBB   *bb = iter->bb;
    MVMSpeshAnn *ann = ins->annotations;

    /* This order of processing is necessary to ensure that a label
     * calculated by one of the control guards as well as the labels
     * calculated below point to the exact same instruction. This is a
     * relatively fragile construction! One could argue that the
     * control guards should in fact use the same (dynamic) labels. */
    while (ann) {
        if (ann->type == MVM_SPESH_ANN_INLINE_END) {
            MVMint32 label = MVM_jit_label_before_ins(tc, jg, bb, ins);
            jg_append_label(tc, jg, label);
            jg->inlines[ann->data.inline_idx].end_label = label;
        } else if (ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS ||
                (ann->type == MVM_SPESH_ANN_DEOPT_INLINE && (ins->info->jittivity & MVM_JIT_INFO_INVOKISH))) {
            /* An underlying assumption here is that this instruction
             * will in fact set the jit_entry_label to a correct
             * value. This is clearly true for invoking ops as well
             * as invokish ops, and in fact there is no other way
             * to get a deopt_all_ins annotation. Still, be warned. */
            MVMint32 label = MVM_jit_label_after_ins(tc, jg, bb, ins);
            jg_append_label(tc, jg, label);
            add_deopt_idx(tc, jg, label, ann->data.deopt_idx);
        }
        ann = ann->next;
    }
}

static void jg_sc_wb(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshOperand check) {
    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                             { MVM_JIT_REG_VAL,     check.reg.orig } };
    jg_append_call_c(tc, jg, &MVM_SC_WB_OBJ, 2, args, MVM_JIT_RV_VOID, -1);
}

static MVMint32 consume_reprop(MVMThreadContext *tc, MVMJitGraph *jg,
                               MVMSpeshIterator *iter, MVMSpeshIns *ins) {
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
        case MVM_OP_bindpos_u:
        case MVM_OP_bindpos_n:
        case MVM_OP_bindpos_s:
        case MVM_OP_bindpos_o:
        case MVM_OP_bindattr_i:
        case MVM_OP_bindattr_u:
        case MVM_OP_bindattr_n:
        case MVM_OP_bindattr_s:
        case MVM_OP_bindattr_o:
        case MVM_OP_bindattrs_i:
        case MVM_OP_bindattrs_u:
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
        case MVM_OP_assign_i:
        case MVM_OP_assign_u:
        case MVM_OP_assign_n:
        case MVM_OP_assign_s:
            type_operand = ins->operands[0];
            break;
        case MVM_OP_atpos_i:
        case MVM_OP_atpos_u:
        case MVM_OP_atpos_n:
        case MVM_OP_atpos_s:
        case MVM_OP_atpos_o:
        case MVM_OP_atkey_i:
        case MVM_OP_atkey_u:
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
        case MVM_OP_getattr_u:
        case MVM_OP_getattr_n:
        case MVM_OP_getattr_s:
        case MVM_OP_getattr_o:
        case MVM_OP_getattrs_i:
        case MVM_OP_getattrs_u:
        case MVM_OP_getattrs_n:
        case MVM_OP_getattrs_s:
        case MVM_OP_getattrs_o:
        case MVM_OP_attrinited:
        case MVM_OP_hintfor:
        case MVM_OP_slice:
        case MVM_OP_decont_i:
        case MVM_OP_decont_n:
        case MVM_OP_decont_s:
        case MVM_OP_decont_u:
            type_operand = ins->operands[1];
            break;
        case MVM_OP_box_i:
        case MVM_OP_box_u:
        case MVM_OP_box_n:
        case MVM_OP_box_s:
            type_operand = ins->operands[2];
            break;
        default:
            MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: not devirtualized (unknown type operand)");
            return 0;

    }

    type_facts = MVM_spesh_get_facts(tc, jg->sg, type_operand);

    if (type_facts && type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && type_facts->type &&
            type_facts->flags & MVM_SPESH_FACT_CONCRETE) {
        switch(op) {
            case MVM_OP_atkey_i:
            case MVM_OP_atkey_u:
            case MVM_OP_atkey_n:
            case MVM_OP_atkey_s:
            case MVM_OP_atkey_o:
                alternative = 1;
                MVM_FALLTHROUGH
            case MVM_OP_atpos_i:
            case MVM_OP_atpos_u:
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

                MVMint32 kind = op == MVM_OP_atpos_i || op == MVM_OP_atkey_i ? MVM_reg_int64 :
                                op == MVM_OP_atpos_u || op == MVM_OP_atkey_u ? MVM_reg_uint64 :
                                op == MVM_OP_atpos_n || op == MVM_OP_atkey_n ? MVM_reg_num64 :
                                op == MVM_OP_atpos_s || op == MVM_OP_atkey_s ? MVM_reg_str :
                                   MVM_reg_obj;

                void *function = NULL;
                MVMuint8 is_double_devirt = 0;

                if (!alternative && ((MVMObject *)type_facts->type)->st->REPR->ID == MVM_REPR_ID_VMArray) {
                    function = MVM_VMArray_find_fast_impl_for_jit(tc, ((MVMObject *)type_facts->type)->st, op, kind);
                    if (function != NULL)
                        is_double_devirt++;
                }
                if (function == NULL) {
                    function = alternative
                        ? (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.at_key
                        : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.at_pos;
                }

                if (is_double_devirt) {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL,  value },
                                             { MVM_JIT_REG_ADDR, dst } };
                    jg_append_call_c(tc, jg, function, 5, args, MVM_JIT_RV_VOID, -1);
                }
                else {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_VAL,     invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL,  value },
                                             { MVM_JIT_REG_ADDR, dst },
                                             { MVM_JIT_LITERAL, kind } };
                    jg_append_call_c(tc, jg, function, 7, args, MVM_JIT_RV_VOID, -1);
                }
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, is_double_devirt ? "JIT: double-devirtualized" : "JIT: devirtualized");
                return 1;
            }
            case MVM_OP_bindkey_i:
            case MVM_OP_bindkey_n:
            case MVM_OP_bindkey_s:
            case MVM_OP_bindkey_o:
                alternative = 1;
                MVM_FALLTHROUGH
            case MVM_OP_bindpos_i:
            case MVM_OP_bindpos_u:
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

                MVMint32 kind = op == MVM_OP_bindpos_i || op == MVM_OP_bindkey_i ? MVM_reg_int64 :
                                op == MVM_OP_bindpos_u ? MVM_reg_int64 :
                                op == MVM_OP_bindpos_n || op == MVM_OP_bindkey_n ? MVM_reg_num64 :
                                op == MVM_OP_bindpos_s || op == MVM_OP_bindkey_s ? MVM_reg_str :
                                                       MVM_reg_obj;

                void *function = NULL;
                MVMuint8 is_double_devirt = 0;

                if (!alternative && ((MVMObject *)type_facts->type)->st->REPR->ID == MVM_REPR_ID_VMArray) {
                    function = MVM_VMArray_find_fast_impl_for_jit(tc, ((MVMObject *)type_facts->type)->st, op, kind);
                    if (function != NULL)
                        is_double_devirt++;
                }
                if (function == NULL) {
                    function = alternative
                        ? (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.bind_key
                        : (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.bind_pos;
                }

                if (is_double_devirt) {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL, key },
                                             { MVM_JIT_REG_VAL, value } };
                    jg_append_call_c(tc, jg, function, 5, args, MVM_JIT_RV_VOID, -1);
                }
                else {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_VAL,     invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL, key },
                                             { MVM_JIT_REG_VAL, value },
                                             { MVM_JIT_LITERAL, kind } };
                    jg_append_call_c(tc, jg, function, 7, args, MVM_JIT_RV_VOID, -1);
                }
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, is_double_devirt ? "JIT: double-devirtualized" : "JIT: devirtualized");
                jg_sc_wb(tc, jg, ins->operands[0]);
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
                jg_append_call_c(tc, jg, function, 4, args, MVM_JIT_RV_INT, dst);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                return 1;
            }
            case MVM_OP_getattr_i:
            case MVM_OP_getattr_u:
            case MVM_OP_getattr_n:
            case MVM_OP_getattr_s:
            case MVM_OP_getattr_o:
            case MVM_OP_getattrs_i:
            case MVM_OP_getattrs_u:
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

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[1]);

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
                                                 op == MVM_OP_getattr_u || op == MVM_OP_getattrs_u ? MVM_reg_uint64 :
                                                 op == MVM_OP_getattr_n || op == MVM_OP_getattrs_n ? MVM_reg_num64 :
                                                 op == MVM_OP_getattr_s || op == MVM_OP_getattrs_s ? MVM_reg_str :
                                                                        MVM_reg_obj } };
                    jg_append_call_c(tc, jg, function, 9, args, MVM_JIT_RV_VOID, -1);
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                    return 1;
                } else {
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: not devirtualized (concreteness not sure)");
                    break;
                }
            }
            case MVM_OP_attrinited: {
                /*attrinited          w(int64) r(obj) r(obj) r(str)*/

                /*MVMint64 (*is_attribute_initialized) (MVMThreadContext *tc, MVMSTable *st,*/
                    /*void *data, MVMObject *class_handle, MVMString *name,*/
                    /*MVMint64 hint);*/

                /* reprconv and interp.c check for concreteness, so we'd either
                 * have to emit a bit of code to check and throw or just rely
                 * on a concreteness fact */

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[1]);

                if (object_facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    MVMint32 dst       = ins->operands[0].reg.orig;
                    MVMint32 invocant  = ins->operands[1].reg.orig;
                    MVMint32 type      = ins->operands[2].reg.orig;
                    MVMint32 attrname  = ins->operands[3].reg.orig;
                    MVMint32 attrhint  = MVM_NO_HINT;

                    void *function = ((MVMObject*)type_facts->type)->st->REPR->attr_funcs.is_attribute_initialized;

                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  invocant },
                                             { MVM_JIT_REG_OBJBODY, invocant },
                                             { MVM_JIT_REG_VAL,     type },
                                             { MVM_JIT_REG_VAL,     attrname },
                                             { MVM_JIT_LITERAL,     attrhint } };
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                    jg_append_call_c(tc, jg, function, 6, args, MVM_JIT_RV_INT, dst);
                    return 1;
                } else {
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: not devirtualized (concreteness not sure)");
                    break;
                }
            }
            case MVM_OP_bindattr_i:
            case MVM_OP_bindattr_u:
            case MVM_OP_bindattr_n:
            case MVM_OP_bindattr_s:
            case MVM_OP_bindattr_o:
            case MVM_OP_bindattrs_i:
            case MVM_OP_bindattrs_u:
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

                MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[0]);

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
                                             { MVM_JIT_REG_VAL,     value },
                                             { MVM_JIT_LITERAL,
                                                 op == MVM_OP_bindattr_i || op == MVM_OP_bindattrs_i ? MVM_reg_int64 :
                                                 op == MVM_OP_bindattr_u || op == MVM_OP_bindattrs_u ? MVM_reg_uint64 :
                                                 op == MVM_OP_bindattr_n || op == MVM_OP_bindattrs_n ? MVM_reg_num64 :
                                                 op == MVM_OP_bindattr_s || op == MVM_OP_bindattrs_s ? MVM_reg_str :
                                                                        MVM_reg_obj } };
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                    jg_append_call_c(tc, jg, function, 9, args, MVM_JIT_RV_VOID, -1);
                    jg_sc_wb(tc, jg, ins->operands[0]);
                    return 1;
                } else {
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: not devirtualized (concreteness not sure)");
                    break;
                }
            }
            case MVM_OP_hintfor: {
                /*
                 *  MVMint64 (*hint_for) (MVMThreadContext *tc, MVMSTable *st,
                 *      MVMObject *class_handle, MVMString *name);
                 */

                MVMint32 result    = ins->operands[0].reg.orig;
                MVMint32 type      = ins->operands[1].reg.orig;
                MVMint32 attrname  = ins->operands[2].reg.orig;

                void *function = ((MVMObject*)type_facts->type)->st->REPR->attr_funcs.hint_for;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  type },
                                         { MVM_JIT_REG_VAL,     type },
                                         { MVM_JIT_REG_VAL,     attrname } };
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                jg_append_call_c(tc, jg, function, 4, args, MVM_JIT_RV_INT, result);
                return 1;
                break;
            }
            case MVM_OP_push_i:
            case MVM_OP_push_n:
            case MVM_OP_push_s:
            case MVM_OP_push_o:
                alternative = 1;
                MVM_FALLTHROUGH
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
                                         { MVM_JIT_REG_VAL,     value },
                                         { MVM_JIT_LITERAL,
                                             op == MVM_OP_push_i || op == MVM_OP_unshift_i ? MVM_reg_int64 :
                                             op == MVM_OP_push_n || op == MVM_OP_unshift_n ? MVM_reg_num64 :
                                             op == MVM_OP_push_s || op == MVM_OP_unshift_s ? MVM_reg_str :
                                                                    MVM_reg_obj } };
                jg_append_call_c(tc, jg, function, 6, args, MVM_JIT_RV_VOID, -1);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                jg_sc_wb(tc, jg, ins->operands[0]);
                return 1;
            }
            case MVM_OP_pop_i:
            case MVM_OP_pop_n:
            case MVM_OP_pop_s:
            case MVM_OP_pop_o:
                alternative = 1;
                MVM_FALLTHROUGH
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
                jg_append_call_c(tc, jg, function, 6, args, MVM_JIT_RV_VOID, -1);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
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
                jg_append_call_c(tc, jg, function, 5, args, MVM_JIT_RV_VOID, -1);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                return 1;
            }
            case MVM_OP_existskey: {
                /*existskey           w(int64) r(obj) r(str) :pure*/
                MVMint32 dst      = ins->operands[0].reg.orig;
                MVMint32 invocant = ins->operands[1].reg.orig;
                MVMint32 keyidx   = ins->operands[2].reg.orig;

                void *function = (void *)((MVMObject*)type_facts->type)->st->REPR->ass_funcs.exists_key;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,     keyidx } };
                jg_append_call_c(tc, jg, function, 5, args, MVM_JIT_RV_INT, dst);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                return 1;
            }
            case MVM_OP_splice: {
                MVMint16 invocant = ins->operands[0].reg.orig;
                MVMint16 source   = ins->operands[1].reg.orig;
                MVMint16 offset   = ins->operands[2].reg.orig;
                MVMint16 count    = ins->operands[3].reg.orig;

                void *function = ((MVMObject*)type_facts->type)->st->REPR->pos_funcs.splice;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                         { MVM_JIT_REG_STABLE,  invocant },
                                         { MVM_JIT_REG_VAL,     invocant },
                                         { MVM_JIT_REG_OBJBODY, invocant },
                                         { MVM_JIT_REG_VAL,     source   },
                                         { MVM_JIT_REG_VAL,     offset   },
                                         { MVM_JIT_REG_VAL,     count    } };
                jg_append_call_c(tc, jg, function, 7, args, MVM_JIT_RV_VOID, -1);
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                return 1;
            }
            case MVM_OP_decont_i:
            case MVM_OP_decont_u:
            case MVM_OP_decont_n:
            case MVM_OP_decont_s: {
                MVMSTable *st = ((MVMObject *)type_facts->type)->st;
                MVMint16 dst = ins->operands[0].reg.orig;
                MVMint16 obj = ins->operands[1].reg.orig;

                MVMuint16 reg_type = op == MVM_OP_decont_i ? MVM_reg_int64
                                   : op == MVM_OP_decont_u ? MVM_reg_uint64
                                   : op == MVM_OP_decont_n ? MVM_reg_num64
                                   :                         MVM_reg_str;

                MVMuint16 ret_type = op == MVM_OP_decont_i ? MVM_JIT_RV_INT
                                   : op == MVM_OP_decont_u ? MVM_JIT_RV_INT
                                   : op == MVM_OP_decont_n ? MVM_JIT_RV_NUM
                                   :                         MVM_JIT_RV_PTR;

                void *function = NULL;

                if (st->container_spec == NULL) {
                    MVM_spesh_graph_add_comment(tc, jg->sg, ins, "JIT: not devirtualized (null container spec)");
                    goto skipdevirt;
                }

                function = MVM_container_devirtualize_fetch_for_jit(tc, st, reg_type);

                if (!function) {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                             { MVM_JIT_REG_VAL, { obj } },
                                             { MVM_JIT_REG_ADDR, { dst } } };

                    function = reg_type == MVM_reg_int64  ? st->container_spec->fetch_i
                             : reg_type == MVM_reg_uint64 ? st->container_spec->fetch_u
                             : reg_type == MVM_reg_num64  ? st->container_spec->fetch_n
                             :                              st->container_spec->fetch_s;

                    jg_append_call_c(tc, jg, function, 3, args, MVM_JIT_RV_VOID, -1);
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                }
                else {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                             { MVM_JIT_REG_VAL, { obj } } };

                    jg_append_call_c(tc, jg, function, 2, args, ret_type, dst);
                    MVM_spesh_graph_add_comment(tc, jg->sg, ins, "JIT: double-devirtualized");
                }
                return 1;
            }
            case MVM_OP_assign_i:
            case MVM_OP_assign_u:
            case MVM_OP_assign_n:
            case MVM_OP_assign_s: {
                MVMSTable *st = ((MVMObject *)type_facts->type)->st;
                MVMint16 dst = ins->operands[0].reg.orig;
                MVMint16 val = ins->operands[1].reg.orig;

                MVMuint16 reg_type = op == MVM_OP_assign_i ? MVM_reg_int64
                                   : op == MVM_OP_assign_u ? MVM_reg_uint64
                                   : op == MVM_OP_assign_n ? MVM_reg_num64
                                   :                         MVM_reg_str;

                void *function = NULL;

                MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                         { MVM_JIT_REG_VAL, { dst } },
                                         { reg_type == MVM_reg_num64 ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, { val } } };

                if (st->container_spec == NULL) {
                    MVM_spesh_graph_add_comment(tc, jg->sg, ins, "JIT: not devirtualized (null container spec)");
                    goto skipdevirt;
                }

                function = MVM_container_devirtualize_store_for_jit(tc, st, reg_type);

                if (!function) {
                    MVM_spesh_graph_add_comment(tc, iter->graph, ins, "JIT: devirtualized");;
                    function = reg_type == MVM_reg_int64  ? (void *)st->container_spec->store_i
                             : reg_type == MVM_reg_uint64 ? (void *)st->container_spec->store_u
                             : reg_type == MVM_reg_num64  ? (void *)st->container_spec->store_n
                             :                              (void *)st->container_spec->store_s;
                }
                else {
                    MVM_spesh_graph_add_comment(tc, jg->sg, ins, "JIT: double-devirtualized");
                }

                jg_append_call_c(tc, jg, function, 3, args, MVM_JIT_RV_VOID, -1);
                return 1;
            }
            default:
                MVM_spesh_graph_add_comment(tc, iter->graph, ins, "not devirtualized in JIT (unimplemented)");
        }
    } else {
        MVM_spesh_graph_add_comment(tc, jg->sg, ins, "JIT: not devirtualized (type unknown)");
    }

skipdevirt:

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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        jg_sc_wb(tc, jg, ins->operands[0]);
        break;
    }
    case MVM_OP_unshift_n:
    case MVM_OP_push_n: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 value    = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL_F, value } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_shift_i:
    case MVM_OP_pop_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_shift_n:
    case MVM_OP_pop_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_deletekey:
    case MVM_OP_setelemspos: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_or_val = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_or_val } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_existskey: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 key = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_slice: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src   = ins->operands[1].reg.orig;
        MVMint16 start = ins->operands[2].reg.orig;
        MVMint16 end   = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, src   },
                                 { MVM_JIT_REG_VAL, start },
                                 { MVM_JIT_REG_VAL, end   } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_existspos:
    case MVM_OP_atkey_i:
    case MVM_OP_atkey_u:
    case MVM_OP_atpos_i:
    case MVM_OP_atpos_u: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 position = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, position } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_NUM, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindkey_n:
    case MVM_OP_bindpos_n: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_pos = ins->operands[1].reg.orig;
        MVMint32 value = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_pos },
                                 { MVM_JIT_REG_VAL_F, value } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        jg_sc_wb(tc, jg, ins->operands[0]);
        break;
    }
    case MVM_OP_bindpos_i:
    case MVM_OP_bindpos_u:
    case MVM_OP_bindpos_s:
    case MVM_OP_bindpos_o:
    case MVM_OP_bindkey_i:
    case MVM_OP_bindkey_s:
    case MVM_OP_bindkey_o: {
        MVMint32 invocant = ins->operands[0].reg.orig;
        MVMint32 key_pos = ins->operands[1].reg.orig;
        MVMint32 value = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, key_pos },
                                 { MVM_JIT_REG_VAL, value } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        jg_sc_wb(tc, jg, ins->operands[0]);
        break;
    }
    case MVM_OP_getattr_i:
    case MVM_OP_getattr_u:
    case MVM_OP_getattr_n:
    case MVM_OP_getattr_s:
    case MVM_OP_getattr_o: {
        MVMuint16 kind = op == MVM_OP_getattr_i ? MVM_JIT_RV_INT :
                         op == MVM_OP_getattr_u ? MVM_JIT_RV_INT :
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, kind, dst);
        break;
    }
    case MVM_OP_getattrs_i:
    case MVM_OP_getattrs_u:
    case MVM_OP_getattrs_n:
    case MVM_OP_getattrs_s:
    case MVM_OP_getattrs_o: {
        MVMuint16 kind = op == MVM_OP_getattrs_i ? MVM_JIT_RV_INT :
                         op == MVM_OP_getattrs_u ? MVM_JIT_RV_INT :
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, kind, dst);
        break;
    }
    case MVM_OP_attrinited: {
        MVMint32 dst       = ins->operands[0].reg.orig;
        MVMint32 invocant  = ins->operands[1].reg.orig;
        MVMint32 type      = ins->operands[2].reg.orig;
        MVMint32 attrname  = ins->operands[3].reg.orig;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant },
                                 { MVM_JIT_REG_VAL, type },
                                 { MVM_JIT_REG_VAL, attrname } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_bindattr_i:
    case MVM_OP_bindattr_u:
    case MVM_OP_bindattr_n:
    case MVM_OP_bindattr_s:
    case MVM_OP_bindattr_o: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[1].reg.orig;
        MVMuint32 str_idx = ins->operands[2].lit_str_idx;
        MVMint16 val = ins->operands[3].reg.orig;
        MVMint16 hint = ins->operands[4].lit_i16;
        MVMuint16 kind = op == MVM_OP_bindattr_i ? MVM_reg_int64 :
                         op == MVM_OP_bindattr_u ? MVM_reg_uint64 :
                         op == MVM_OP_bindattr_n ? MVM_reg_num64 :
                         op == MVM_OP_bindattr_s ? MVM_reg_str :
                         /* MVM_OP_bindattr_o ? */ MVM_reg_obj;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_STR_IDX, str_idx },
                                 { MVM_JIT_LITERAL, hint },
                                 { MVM_JIT_REG_VAL, val }, /* Takes MVMRegister, so no _F needed. */
                                 { MVM_JIT_LITERAL, kind } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
        jg_sc_wb(tc, jg, ins->operands[0]);
        break;
    }
    case MVM_OP_bindattrs_i:
    case MVM_OP_bindattrs_u:
    case MVM_OP_bindattrs_n:
    case MVM_OP_bindattrs_s:
    case MVM_OP_bindattrs_o: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[1].reg.orig;
        MVMint16 str = ins->operands[2].reg.orig;
        MVMint16 val = ins->operands[3].reg.orig;
        MVMint16 hint = -1;
        MVMuint16 kind = op == MVM_OP_bindattrs_i ? MVM_reg_int64 :
                         op == MVM_OP_bindattrs_u ? MVM_reg_uint64 :
                         op == MVM_OP_bindattrs_n ? MVM_reg_num64 :
                         op == MVM_OP_bindattrs_s ? MVM_reg_str :
                         /* MVM_OP_bindattrs_o ? */ MVM_reg_obj;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj },
                                 { MVM_JIT_REG_VAL, typ },
                                 { MVM_JIT_REG_VAL, str },
                                 { MVM_JIT_LITERAL, hint },
                                 { MVM_JIT_REG_VAL, val }, /* Takes MVMRegister, so no _F needed. */
                                 { MVM_JIT_LITERAL, kind } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
        jg_sc_wb(tc, jg, ins->operands[0]);
        break;
    }
    case MVM_OP_hintfor: {
        MVMint16 dst      = ins->operands[0].reg.orig;
        MVMint32 type     = ins->operands[1].reg.orig;
        MVMint32 attrname = ins->operands[2].reg.orig;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL,     type },
                                 { MVM_JIT_REG_VAL,     attrname } };

        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_elems: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_decont_i:
    case MVM_OP_decont_u:
    case MVM_OP_decont_n:
    case MVM_OP_decont_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_ADDR, { dst } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_assign_i:
    case MVM_OP_assign_u:
    case MVM_OP_assign_n:
    case MVM_OP_assign_s: {
        MVMint16 target = ins->operands[0].reg.orig;
        MVMint16 value  = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { op == MVM_OP_assign_n ? MVM_JIT_REG_VAL_F : MVM_JIT_REG_VAL, { value } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    default:
        return 0;
    }

    return 1;
}

static void add_bail_comment(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIns *ins) {
    MVMSpeshGraph *g = jg->sg;
    if (MVM_spesh_debug_enabled(tc)) {
        MVM_spesh_graph_add_comment(tc, g, ins, "JIT: bailed completely because of <%s>",
                                    ins->info->name);
    }
}

static MVMint32 consume_ins(MVMThreadContext *tc, MVMJitGraph *jg,
                            MVMSpeshIterator *iter, MVMSpeshIns *ins) {
    MVMint16 op;
    op = ins->info->opcode;
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
    case MVM_OP_pow_i:
    case MVM_OP_abs_i:
    case MVM_OP_add_n:
    case MVM_OP_sub_n:
    case MVM_OP_mul_n:
    case MVM_OP_div_n:
    case MVM_OP_neg_n:
        /* number coercion */
    case MVM_OP_coerce_iu:
    case MVM_OP_coerce_ui:
    case MVM_OP_coerce_ni:
    case MVM_OP_coerce_in:
    case MVM_OP_extend_i8:
    case MVM_OP_extend_u8:
    case MVM_OP_extend_i16:
    case MVM_OP_extend_u16:
    case MVM_OP_extend_i32:
    case MVM_OP_extend_u32:
    case MVM_OP_trunc_i8:
    case MVM_OP_trunc_u8:
    case MVM_OP_trunc_i16:
    case MVM_OP_trunc_u16:
    case MVM_OP_trunc_i32:
    case MVM_OP_trunc_u32:
    case MVM_OP_sp_istrue_n:
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
        /* marking code */
    case MVM_OP_markcodestatic:
    case MVM_OP_markcodestub:
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
    case MVM_OP_nan:
    case MVM_OP_inf:
    case MVM_OP_neginf:
    case MVM_OP_const_s:
    case MVM_OP_null:
    case MVM_OP_sp_resumption:
        /* accessors */
    case MVM_OP_sp_p6oget_o:
    case MVM_OP_sp_p6oget_s:
    case MVM_OP_sp_p6oget_i:
    case MVM_OP_sp_p6oget_n:
    case MVM_OP_sp_p6ogetvc_o:
    case MVM_OP_sp_p6ogetvt_o:
    case MVM_OP_sp_p6oget_bi:
    case MVM_OP_sp_p6oget_i32:
    case MVM_OP_sp_getvc_o:
    case MVM_OP_sp_getvt_o:
    case MVM_OP_sp_p6obind_i:
    case MVM_OP_sp_p6obind_n:
    case MVM_OP_sp_p6obind_s:
    case MVM_OP_sp_p6obind_o:
    case MVM_OP_sp_p6obind_i32:
    case MVM_OP_sp_bind_i64:
    case MVM_OP_sp_bind_i32:
    case MVM_OP_sp_bind_n:
    case MVM_OP_sp_bind_s:
    case MVM_OP_sp_bind_s_nowb:
    case MVM_OP_sp_bind_o:
    case MVM_OP_sp_get_i64:
    case MVM_OP_sp_get_i32:
    case MVM_OP_sp_get_n:
    case MVM_OP_sp_get_s:
    case MVM_OP_sp_get_o:
    case MVM_OP_sp_deref_bind_i64:
    case MVM_OP_sp_deref_bind_n:
    case MVM_OP_sp_deref_get_i64:
    case MVM_OP_sp_deref_get_n:
    case MVM_OP_set:
    case MVM_OP_getlex:
    case MVM_OP_sp_getlex_o:
    case MVM_OP_sp_getlex_ins:
    case MVM_OP_sp_getlexvia_o:
    case MVM_OP_sp_getlexvia_ins:
    case MVM_OP_getlexrel:
    case MVM_OP_getlexreldyn:
    case MVM_OP_getlexrelcaller:
    case MVM_OP_getlex_no:
    case MVM_OP_sp_getlex_no:
    case MVM_OP_bindlex:
    case MVM_OP_sp_bindlex_os:
    case MVM_OP_sp_bindlex_in:
    case MVM_OP_sp_bindlexvia_os:
    case MVM_OP_sp_bindlexvia_in:
    case MVM_OP_getwhat:
    case MVM_OP_getwho:
    case MVM_OP_getwhere:
    case MVM_OP_sp_getspeshslot:
    case MVM_OP_ctx:
    case MVM_OP_ctxlexpad:
    case MVM_OP_ctxcallerskipthunks:
    case MVM_OP_curcode:
    case MVM_OP_getcode:
    case MVM_OP_sp_fastcreate:
    case MVM_OP_iscont:
    case MVM_OP_decont:
    case MVM_OP_sp_decont:
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
    case MVM_OP_lexprimspec:
    case MVM_OP_objprimspec:
    case MVM_OP_objprimbits:
    case MVM_OP_objprimunsigned:
    case MVM_OP_takehandlerresult:
    case MVM_OP_exception:
    case MVM_OP_scgethandle:
    case MVM_OP_scobjcount:
    case MVM_OP_setobjsc:
    case MVM_OP_scwbdisable:
    case MVM_OP_scwbenable:
    case MVM_OP_assign:
    case MVM_OP_assignunchecked:
    case MVM_OP_getlexperinvtype_o:
    case MVM_OP_paramnamesused:
    case MVM_OP_sp_assertparamcheck:
    case MVM_OP_getobjsc:
    case MVM_OP_getstderr:
    case MVM_OP_getstdout:
    case MVM_OP_getstdin:
    case MVM_OP_ordat:
    case MVM_OP_ordbaseat:
    case MVM_OP_ordfirst:
    case MVM_OP_getcodename:
    case MVM_OP_setcodeobj:
    case MVM_OP_setcodename:
    case MVM_OP_hllbool:
    case MVM_OP_sp_getlexstatic_o:
    case MVM_OP_newtype:
    case MVM_OP_newmixintype:
    case MVM_OP_composetype:
    case MVM_OP_getcomp:
        /* Profiling */
    case MVM_OP_prof_enterspesh:
    case MVM_OP_prof_enterinline:
    case MVM_OP_prof_replaced:
        /* Exception handling */
    case MVM_OP_lastexpayload:
        /* Parameters */
    case MVM_OP_param_sp:
    case MVM_OP_param_sn:
    case MVM_OP_sp_bindcomplete:
        /* Specialized atomics */
    case MVM_OP_sp_cas_o:
    case MVM_OP_sp_atomicload_o:
    case MVM_OP_sp_atomicstore_o:
        /* Specialized boxings */
    case MVM_OP_sp_fastbox_i:
    case MVM_OP_sp_fastbox_i_ic:
    case MVM_OP_sp_fastbox_bi:
    case MVM_OP_sp_fastbox_bi_ic:
        /* Specialized boxings */
    case MVM_OP_sp_add_I:
    case MVM_OP_sp_sub_I:
    case MVM_OP_sp_mul_I:
    case MVM_OP_sp_bool_I:
        /* Specialized argument reading */
    case MVM_OP_sp_getarg_i:
    case MVM_OP_sp_getarg_n:
    case MVM_OP_sp_getarg_s:
    case MVM_OP_sp_getarg_o:
        jg_append_primitive(tc, jg, ins);
        break;
        /* Unspecialized parameter access */
    case MVM_OP_param_rp_i: {
        MVMint16  dst     = ins->operands[0].reg.orig;
        MVMuint16 arg_idx = ins->operands[1].lit_ui16;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { arg_idx } } };
        jg_append_call_c(tc, jg, MVM_args_get_required_pos_int, 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_param_rp_u: {
        MVMint16  dst     = ins->operands[0].reg.orig;
        MVMuint16 arg_idx = ins->operands[1].lit_ui16;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { arg_idx } } };
        jg_append_call_c(tc, jg, MVM_args_get_required_pos_uint, 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_param_rp_s: {
        MVMint16  dst     = ins->operands[0].reg.orig;
        MVMuint16 arg_idx = ins->operands[1].lit_ui16;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { arg_idx } } };
        jg_append_call_c(tc, jg, MVM_args_get_required_pos_str, 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_param_rp_o: {
        MVMint16  dst     = ins->operands[0].reg.orig;
        MVMuint16 arg_idx = ins->operands[1].lit_ui16;

        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { arg_idx } } };
        jg_append_call_c(tc, jg, MVM_args_get_required_pos_obj, 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
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
    case MVM_OP_if_s:
    case MVM_OP_unless_s:
        jg_append_branch(tc, jg, 0, ins);
        break;
        /* some functions */
    case MVM_OP_gethow: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_gethllsym: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 hll = ins->operands[1].reg.orig;
        MVMint16 sym = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { hll } },
                                 { MVM_JIT_REG_VAL, { sym } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_checkarity: {
        MVMuint16 min = ins->operands[0].lit_i16;
        MVMuint16 max = ins->operands[1].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_PARAMS } },
                                 { MVM_JIT_LITERAL, { min } },
                                 { MVM_JIT_LITERAL, { max } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_say:
    case MVM_OP_print: {
        MVMint32 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_scgetobjidx: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 sc  = ins->operands[1].reg.orig;
        MVMint64 obj = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { sc } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_throwdyn:
    case MVM_OP_throwlex:
    case MVM_OP_throwlexotic: {
        MVMint16 regi   = ins->operands[0].reg.orig;
        MVMint16 object = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, {
                                   op == MVM_OP_throwlexotic ? MVM_EX_THROW_LEXOTIC :
                                   op == MVM_OP_throwlex     ? MVM_EX_THROW_LEX :
                                                               MVM_EX_THROW_DYN
                                   } },
                                 { MVM_JIT_REG_VAL, { object } },
                                 { MVM_JIT_REG_ADDR, { regi } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op),
                          4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_rethrow: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { MVM_EX_THROW_DYN } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_LITERAL, { 0 } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_throwcatdyn:
    case MVM_OP_throwcatlex:
    case MVM_OP_throwcatlexotic: {
        MVMint16 regi     = ins->operands[0].reg.orig;
        MVMint32 category = (MVMuint32)ins->operands[1].lit_i64;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, {
                                   op == MVM_OP_throwcatdyn ? MVM_EX_THROW_DYN :
                                   op == MVM_OP_throwcatlex ? MVM_EX_THROW_LEX :
                                                              MVM_EX_THROW_LEXOTIC
                                   } },
                                 { MVM_JIT_LITERAL, { category } },
                                 { MVM_JIT_REG_ADDR, { regi } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op),
                          4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_throwpayloadlex:
    case MVM_OP_throwpayloadlexcaller: {
        MVMint16 regi     = ins->operands[0].reg.orig;
        MVMint32 category = (MVMuint32)ins->operands[1].lit_i64;
        MVMint16 payload  = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { op == MVM_OP_throwpayloadlex
                                                        ? MVM_EX_THROW_LEX
                                                        : MVM_EX_THROW_LEX_CALLER } },
                                 { MVM_JIT_LITERAL, { category } },
                                 { MVM_JIT_REG_VAL, { payload } },
                                 { MVM_JIT_REG_ADDR, { regi } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op),
                          5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getexpayload: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindexpayload: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 payload = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { payload } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getexmessage: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindexmessage: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMint16 payload = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { payload } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_resume: {
        MVMint16 exc = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { exc } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_die: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 str = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_REG_ADDR, { dst } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op),
                          3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getdynlex: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CALLER } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_binddynlex: {
        MVMint16 name = ins->operands[0].reg.orig;
        MVMint16 val  = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_REG_VAL, { val }  },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CALLER } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op),
                          4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getlexouter: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getlexcaller: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_CALLER } } };
        jg_append_call_c(tc, jg, MVM_frame_find_lexical_by_name_rel_caller,
                         3, args, MVM_JIT_RV_DEREF_OR_VMNULL, dst);
        break;
    }
    case MVM_OP_capturelex: {
        MVMint16 code = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { code } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_captureinnerlex: {
        MVMint16 code = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { code } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_takeclosure: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_usecapture:
    case MVM_OP_savecapture: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_FRAME } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_captureposelems: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_captureposarg_s:
    case MVM_OP_captureposarg: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMint16 idx     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_captureposarg_i: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMint16 idx     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_captureposarg_n: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMint16 idx     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_captureposprimspec: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMint16 index   = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { index } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_captureexistsnamed: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMint16 name    = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } },
                                 { MVM_JIT_REG_VAL, { name } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_capturehasnameds: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 capture = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { capture } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_gt_s:
    case MVM_OP_ge_s:
    case MVM_OP_lt_s:
    case MVM_OP_le_s:
    case MVM_OP_cmp_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 a   = ins->operands[1].reg.orig;
        MVMint16 b   = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { a } },
                                 { MVM_JIT_REG_VAL, { b } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        /* We rely on an implementation of the comparisons against -1, 0 and 1
         * in emit.dasc */
        if (op != MVM_OP_cmp_s) {
            jg_append_primitive(tc, jg, ins);
        }
        break;
    }
    case MVM_OP_hllboolfor: {
        MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[2]);
        if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
            MVMHLLConfig *hll_config = MVM_hll_get_config_for(tc, facts->value.s);
            ins->operands[2].lit_i64 = (MVMint64)hll_config;
            jg_append_primitive(tc, jg, ins);
        } else {
            add_bail_comment(tc, jg, ins);
            return 0;
        }
        break;
    }
    case MVM_OP_istrue_s:
    case MVM_OP_isfalse_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args,
                op == MVM_OP_istrue_s ? MVM_JIT_RV_INT : MVM_JIT_RV_INT_NEGATED,
                dst);
        break;
    }
    case MVM_OP_iscoderef: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_clone: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_create: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 type = ins->operands[1].reg.orig;
        MVMJitCallArg args_alloc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } } };
        MVMJitCallArg args_init[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { dst } } };
        jg_append_call_c(tc, jg, MVM_repr_alloc, 2, args_alloc, MVM_JIT_RV_PTR, dst);
        jg_append_call_c(tc, jg, MVM_repr_init, 2, args_init, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_cas_o: {
        MVMint16 result = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMint16 expected = ins->operands[2].reg.orig;
        MVMint16 value = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { expected } },
                                 { MVM_JIT_REG_VAL, { value } },
                                 { MVM_JIT_REG_ADDR, { result } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_cas_i: {
        MVMint16 result = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMint16 expected = ins->operands[2].reg.orig;
        MVMint16 value = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { expected } },
                                 { MVM_JIT_REG_VAL, { value } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, result);
        break;
    }
    case MVM_OP_atomicinc_i:
    case MVM_OP_atomicdec_i:
    case MVM_OP_atomicload_i: {
        MVMint16 result = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, result);
        break;
    }
    case MVM_OP_atomicadd_i: {
        MVMint16 result = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMint16 increment = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { increment } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, result);
        break;
    }
    case MVM_OP_atomicload_o: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 target = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_atomicstore_o:
    case MVM_OP_atomicstore_i: {
        MVMint16 target = ins->operands[0].reg.orig;
        MVMint16 value = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { target } },
                                 { MVM_JIT_REG_VAL, { value } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_lock: {
        MVMint16 lock = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { lock } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_unlock: {
        MVMint16 lock = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { lock } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_writeuint:
    case MVM_OP_writeint: {
        MVMint16 buf   = ins->operands[0].reg.orig;
        MVMint16 off   = ins->operands[1].reg.orig;
        MVMint16 value = ins->operands[2].reg.orig;
        MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[3]);
        if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
            if ((facts->value.i & 3) != MVM_SWITCHENDIAN) {
                unsigned char const size  = 1 << (facts->value.i >> 2);

                MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[0]);
                if (type_facts && type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && type_facts->type &&
                        type_facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    void *function = (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.write_buf;

                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  buf },
                                             { MVM_JIT_REG_VAL,     buf },
                                             { MVM_JIT_REG_OBJBODY, buf },
                                             { MVM_JIT_REG_ADDR,    value },
                                             { MVM_JIT_REG_VAL,     off },
                                             { MVM_JIT_LITERAL,     size } };
                    jg_append_call_c(tc, jg, function, 7, args, MVM_JIT_RV_VOID, -1);
                }
                else {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_VAL,     buf },
                                             { MVM_JIT_REG_ADDR,    value },
                                             { MVM_JIT_REG_VAL,     off },
                                             { MVM_JIT_LITERAL,     size } };
                    jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
                }
            }
            else {
                MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                                "BAIL: op <%s>, - endian switching not yet supported by JIT",
                                ins->info->name);
                return 0;
            }
        }
        else {
            MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                            "BAIL: op <%s>, - no known value for writeint flags",
                            ins->info->name);
            return 0;
        }
        break;
    }
    case MVM_OP_readuint: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 buf   = ins->operands[1].reg.orig;
        MVMint16 off   = ins->operands[2].reg.orig;
        MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[3]);
        if (facts->flags & MVM_SPESH_FACT_KNOWN_VALUE) {
            if ((facts->value.i & 3) != MVM_SWITCHENDIAN) {
                unsigned char const size  = 1 << (facts->value.i >> 2);

                MVMSpeshFacts *type_facts = MVM_spesh_get_facts(tc, jg->sg, ins->operands[0]);
                if (type_facts && type_facts->flags & MVM_SPESH_FACT_KNOWN_TYPE && type_facts->type &&
                        type_facts->flags & MVM_SPESH_FACT_CONCRETE) {
                    void *function = (void *)((MVMObject*)type_facts->type)->st->REPR->pos_funcs.read_buf;

                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_STABLE,  buf },
                                             { MVM_JIT_REG_VAL,     buf },
                                             { MVM_JIT_REG_OBJBODY, buf },
                                             { MVM_JIT_REG_VAL,     off },
                                             { MVM_JIT_LITERAL,     size } };
                    jg_append_call_c(tc, jg, function, 6, args, MVM_JIT_RV_INT, dst);
                }
                else {
                    MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  MVM_JIT_INTERP_TC },
                                             { MVM_JIT_REG_VAL,     buf },
                                             { MVM_JIT_REG_VAL,     off },
                                             { MVM_JIT_LITERAL,     size } };
                    jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
                }
            }
            else {
                MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                                "BAIL: op <%s>, - endian switching not yet supported by JIT",
                                ins->info->name);
                return 0;
            }
        }
        else {
            MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                            "BAIL: op <%s>, - no known value for readint flags",
                            ins->info->name);
            return 0;
        }
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
    case MVM_OP_slice:
    case MVM_OP_splice:
    case MVM_OP_atpos_i:
    case MVM_OP_atpos_u:
    case MVM_OP_atpos_n:
    case MVM_OP_atpos_s:
    case MVM_OP_atpos_o:
    case MVM_OP_atkey_i:
    case MVM_OP_atkey_u:
    case MVM_OP_atkey_n:
    case MVM_OP_atkey_s:
    case MVM_OP_atkey_o:
    case MVM_OP_bindpos_i:
    case MVM_OP_bindpos_u:
    case MVM_OP_bindpos_n:
    case MVM_OP_bindpos_s:
    case MVM_OP_bindpos_o:
    case MVM_OP_bindkey_i:
    case MVM_OP_bindkey_n:
    case MVM_OP_bindkey_s:
    case MVM_OP_bindkey_o:
    case MVM_OP_getattr_i:
    case MVM_OP_getattr_u:
    case MVM_OP_getattr_n:
    case MVM_OP_getattr_s:
    case MVM_OP_getattr_o:
    case MVM_OP_getattrs_i:
    case MVM_OP_getattrs_u:
    case MVM_OP_getattrs_n:
    case MVM_OP_getattrs_s:
    case MVM_OP_getattrs_o:
    case MVM_OP_attrinited:
    case MVM_OP_bindattr_i:
    case MVM_OP_bindattr_u:
    case MVM_OP_bindattr_n:
    case MVM_OP_bindattr_s:
    case MVM_OP_bindattr_o:
    case MVM_OP_bindattrs_i:
    case MVM_OP_bindattrs_u:
    case MVM_OP_bindattrs_n:
    case MVM_OP_bindattrs_s:
    case MVM_OP_bindattrs_o:
    case MVM_OP_hintfor:
    case MVM_OP_elems:
    case MVM_OP_decont_i:
    case MVM_OP_decont_u:
    case MVM_OP_decont_n:
    case MVM_OP_decont_s:
    case MVM_OP_assign_i:
    case MVM_OP_assign_u:
    case MVM_OP_assign_n:
    case MVM_OP_assign_s:
        if (!consume_reprop(tc, jg, iter, ins)) {
            add_bail_comment(tc, jg, ins);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_continuationreset: {
        MVMint16 reg  = ins->operands[0].reg.orig;
        MVMint16 tag  = ins->operands[1].reg.orig;
        MVMint16 code = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { tag } },
                                 { MVM_JIT_REG_VAL, { code } },
                                 { MVM_JIT_REG_ADDR, { reg } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_continuationcontrol: {
        MVMint16 reg  = ins->operands[0].reg.orig;
        MVMint16 protect  = ins->operands[1].reg.orig;
        MVMint16 tag  = ins->operands[2].reg.orig;
        MVMint16 code = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { protect } },
                                 { MVM_JIT_REG_VAL, { tag } },
                                 { MVM_JIT_REG_VAL, { code } },
                                 { MVM_JIT_REG_ADDR, { reg } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_continuationinvoke: {
        MVMint16 reg  = ins->operands[0].reg.orig;
        MVMint16 cont  = ins->operands[1].reg.orig;
        MVMint16 code = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { cont } },
                                 { MVM_JIT_REG_VAL, { code } },
                                 { MVM_JIT_REG_ADDR, { reg } },
                                 { MVM_JIT_LITERAL_PTR, { 0 } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_sp_boolify_iter: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }

        /* coercion */
    case MVM_OP_coerce_sn:
    case MVM_OP_coerce_ns:
    case MVM_OP_coerce_si:
    case MVM_OP_coerce_su:
    case MVM_OP_coerce_is:
    case MVM_OP_coerce_us:
    case MVM_OP_coerce_In: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        MVMJitRVMode rv_mode = ((op == MVM_OP_coerce_sn || op == MVM_OP_coerce_In) ? MVM_JIT_RV_NUM :
                                op == MVM_OP_coerce_si || op == MVM_OP_coerce_su ? MVM_JIT_RV_INT :
                                MVM_JIT_RV_PTR);
        if (op == MVM_OP_coerce_ns) {
            args[1].type = MVM_JIT_REG_VAL_F;
        }
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
    case MVM_OP_coerce_nI: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                { MVM_JIT_REG_VAL,   { typ } },
                                { MVM_JIT_REG_VAL_F, { src } }};

        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_coerce_sI: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                { MVM_JIT_REG_VAL, { src } },
                                { MVM_JIT_REG_VAL, { typ } }};

        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_coerce_II: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 typ = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                { MVM_JIT_REG_VAL, { typ } },
                                { MVM_JIT_REG_VAL, { src } }};

        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_queuepoll: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getuniprop_int:
    case MVM_OP_getuniprop_bool: {
        MVMint16 dst       = ins->operands[0].reg.orig;
        MVMint16 grapheme  = ins->operands[1].reg.orig;
        MVMint16 prop_code = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { grapheme } },
                                 { MVM_JIT_REG_VAL, { prop_code } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_getuniprop_str: {
        MVMint16 dst       = ins->operands[0].reg.orig;
        MVMint16 grapheme  = ins->operands[1].reg.orig;
        MVMint16 prop_code = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { grapheme } },
                                 { MVM_JIT_REG_VAL, { prop_code } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_stat:
    case MVM_OP_lstat: {
        MVMint16 dst      = ins->operands[0].reg.orig;
        MVMint16 filename = ins->operands[1].reg.orig;
        MVMint16 status   = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { filename } },
                                 { MVM_JIT_REG_VAL, { status } },
                                 { MVM_JIT_LITERAL, { op == MVM_OP_stat ? 0 : 1 } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_open_dir: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 path = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { path } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_read_dir: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 dho = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { dho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_close_dir: {
        MVMint16 fho = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_close_fh: {
        MVMint16 fho = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_open_fh: {
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMint16 path = ins->operands[1].reg.orig;
        MVMint16 mode = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { path } },
                                 { MVM_JIT_REG_VAL, { mode } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_eof_fh: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 fho = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_istty_fh: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 fho = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_fileno_fh: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 fho = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_write_fhb: {
        MVMint16 fho = ins->operands[0].reg.orig;
        MVMint16 buf = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } },
                                 { MVM_JIT_REG_VAL, { buf } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_read_fhb: {
        MVMint16 fho = ins->operands[0].reg.orig;
        MVMint16 res = ins->operands[1].reg.orig;
        MVMint16 len = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } },
                                 { MVM_JIT_REG_VAL, { res } },
                                 { MVM_JIT_REG_VAL, { len } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_seek_fh: {
        MVMint16 fho    = ins->operands[0].reg.orig;
        MVMint16 offset = ins->operands[1].reg.orig;
        MVMint16 flag   = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { fho } },
                                 { MVM_JIT_REG_VAL, { offset } },
                                 { MVM_JIT_REG_VAL, { flag } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_box_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 val = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL_F, { val } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_box_s:
    case MVM_OP_box_i:
    case MVM_OP_box_u: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 val = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { val } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_unbox_i: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_unbox_u: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_unbox_n: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_unbox_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR , { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_escape:
    case MVM_OP_uc:
    case MVM_OP_lc:
    case MVM_OP_tc:
    case MVM_OP_fc:
    case MVM_OP_indexingoptimized: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 string = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { string } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, MVM_OP_eq_s), 3, args,
                          MVM_JIT_RV_INT, dst);
        if (op == MVM_OP_ne_s) {
            /* append not_i to negate ne_s */
            MVMSpeshIns *not_i          = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMSpeshIns));
            not_i->info                 = MVM_op_get_op(MVM_OP_not_i);
            not_i->operands             = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMSpeshOperand) * 2);
            not_i->operands[0].reg.orig = dst;
            not_i->operands[1].reg.orig = dst;
            jg_append_primitive(tc, jg, not_i);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_eqatic_s: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                         MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_eqatim_s: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                         MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_eqaticim_s: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } },
                                 { MVM_JIT_REG_VAL, { offset } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                         MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_chars:
    case MVM_OP_codes_s:
    case MVM_OP_flip: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        MVMJitRVMode rv_mode = (op == MVM_OP_flip ? MVM_JIT_RV_PTR : MVM_JIT_RV_INT);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, rv_mode, dst);
        break;
    }
    case MVM_OP_getcp_s: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 idx = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_REG_VAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_chr: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_join: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 sep   = ins->operands[1].reg.orig;
        MVMint16 input = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { sep } },
                                 { MVM_JIT_REG_VAL, { input } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_replace: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 a       = ins->operands[1].reg.orig;
        MVMint16 start   = ins->operands[2].reg.orig;
        MVMint16 length  = ins->operands[3].reg.orig;
        MVMint16 replace = ins->operands[4].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { a } },
                                 { MVM_JIT_REG_VAL, { start } },
                                 { MVM_JIT_REG_VAL, { length } },
                                 { MVM_JIT_REG_VAL, { replace } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_INT, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_INT, dst);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 7, args, MVM_JIT_RV_VOID, -1);
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_nfafromstatelist: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 states  = ins->operands[1].reg.orig;
        MVMint16 type    = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { states } },
                                 { MVM_JIT_REG_VAL, { type } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* encode/decode ops */
    case MVM_OP_encode: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 str = ins->operands[1].reg.orig;
        MVMint16 enc = ins->operands[2].reg.orig;
        MVMint16 buf = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_REG_VAL, { enc } },
                                 { MVM_JIT_REG_VAL, { buf } },
                                 { MVM_JIT_LITERAL_PTR, { 0 } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decoderaddbytes: {
        MVMint16 decoder = ins->operands[0].reg.orig;
        MVMint16 bytes   = ins->operands[1].reg.orig;
        MVMJitCallArg argc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)"decoderaddbytes" } } };
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_REG_VAL, { bytes } } };
        jg_append_call_c(tc, jg, &MVM_decoder_ensure_decoder, 3, argc, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_decodertakebytes: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 decoder = ins->operands[1].reg.orig;
        MVMint16 chomp   = ins->operands[2].reg.orig;
        MVMint16 inc     = ins->operands[3].reg.orig;
        MVMJitCallArg argc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)"decodertakebytes" } } };
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_REG_VAL, { chomp } },
                                 { MVM_JIT_REG_VAL, { inc } } };
        jg_append_call_c(tc, jg, &MVM_decoder_ensure_decoder, 3, argc, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decodertakeallchars: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 decoder = ins->operands[1].reg.orig;
        MVMJitCallArg argc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)"decodertakeallchars" } } };
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } } };
        jg_append_call_c(tc, jg, &MVM_decoder_ensure_decoder, 3, argc, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decodertakeline: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 decoder = ins->operands[1].reg.orig;
        MVMint16 chomp   = ins->operands[2].reg.orig;
        MVMint16 inc     = ins->operands[3].reg.orig;
        MVMJitCallArg argc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)"decodertakeline" } } };
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_REG_VAL, { chomp } },
                                 { MVM_JIT_REG_VAL, { inc } } };
        jg_append_call_c(tc, jg, &MVM_decoder_ensure_decoder, 3, argc, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decoderempty: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 decoder = ins->operands[1].reg.orig;
        MVMJitCallArg argc[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)"decodertakeline" } } };
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { decoder } } };
        jg_append_call_c(tc, jg, &MVM_decoder_ensure_decoder, 3, argc, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* bigint ops */
    case MVM_OP_isbig_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args,
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_add_I:
    case MVM_OP_sub_I:
    case MVM_OP_mul_I:
    case MVM_OP_div_I:
    case MVM_OP_mod_I:
    case MVM_OP_bor_I:
    case MVM_OP_band_I:
    case MVM_OP_bxor_I:
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_neg_I:
    case MVM_OP_abs_I: {
        MVMint16 src  = ins->operands[1].reg.orig;
        MVMint16 type = ins->operands[2].reg.orig;
        MVMint16 dst  = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_pow_I: {
        MVMint16 src_a  = ins->operands[1].reg.orig;
        MVMint16 src_b  = ins->operands[2].reg.orig;
        MVMint16 type_n = ins->operands[3].reg.orig;
        MVMint16 type_I = ins->operands[4].reg.orig;
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src_a } },
                                 { MVM_JIT_REG_VAL, { src_b } },
                                 { MVM_JIT_REG_VAL, { type_n } },
                                 { MVM_JIT_REG_VAL, { type_I } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args,
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_brshift_I:
    case MVM_OP_blshift_I: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src   = ins->operands[1].reg.orig;
        MVMint16 shift = ins->operands[2].reg.orig;
        MVMint16 type  = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_REG_VAL, { shift } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_coerce_Is: {
        MVMint16 src = ins->operands[1].reg.orig;
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } },
                                 { MVM_JIT_LITERAL, { 10 } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_radix: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 radix = ins->operands[1].reg.orig;
        MVMint16 string = ins->operands[2].reg.orig;
        MVMint16 offset = ins->operands[3].reg.orig;
        MVMint16 flag = ins->operands[4].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { radix } },
                                 { MVM_JIT_REG_VAL, { string } },
                                 { MVM_JIT_REG_VAL, { offset } },
                                 { MVM_JIT_REG_VAL, { flag } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args,
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 6, args,
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
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_isprime_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_bool_I: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_rand_I:
    case MVM_OP_bnot_I: {
        MVMint16 dst      = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMint32 type     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, type },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getcodeobj: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint32 invocant = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, invocant } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_abs_n:
    case MVM_OP_ceil_n:
    case MVM_OP_floor_n:
    case MVM_OP_sqrt_n:
#ifndef MVM_HAS_SUBSTANDARD_LOG
    case MVM_OP_log_n:
#endif
    case MVM_OP_exp_n:
    case MVM_OP_sin_n:
    case MVM_OP_cos_n:
    case MVM_OP_tan_n:
#ifndef MVM_HAS_SUBSTANDARD_ASIN
    case MVM_OP_asin_n:
#endif
#ifndef MVM_HAS_SUBSTANDARD_ACOS
    case MVM_OP_acos_n:
#endif
    case MVM_OP_atan_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src   = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_REG_VAL_F, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args,
                          MVM_JIT_RV_NUM, dst);
        break;
    }
#ifndef MVM_HAS_SUBSTANDARD_POW
    case MVM_OP_pow_n:
#endif
    case MVM_OP_atan2_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 a     = ins->operands[1].reg.orig;
        MVMint16 b     = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_REG_VAL_F, { a } },
                                 { MVM_JIT_REG_VAL_F, { b } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args,
                          MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_time: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args,
                          MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_randscale_n: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 scale = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL_F, { scale } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_NUM, dst);
        break;
    }
    case MVM_OP_isnanorinf: {
        MVMint16 dst   = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL_F, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_nativecallcast: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 restype = ins->operands[1].reg.orig;
        MVMint16 site    = ins->operands[2].reg.orig;
        MVMint16 cargs   = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { restype } },
                                 { MVM_JIT_REG_VAL, { site } },
                                 { MVM_JIT_REG_VAL, { cargs } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_nativecallinvoke: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 restype = ins->operands[1].reg.orig;
        MVMint16 site    = ins->operands[2].reg.orig;
        MVMint16 cargs   = ins->operands[3].reg.orig;
        MVMJitCallArg targs[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, MVM_jit_code_trampoline, 1, targs, MVM_JIT_RV_VOID, -1);
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { restype } },
                                 { MVM_JIT_REG_VAL, { site } },
                                 { MVM_JIT_REG_VAL, { cargs } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args,
                          MVM_JIT_RV_PTR, dst);
        jg_append_deopt_check(tc, jg);
        break;
    }
    case MVM_OP_typeparameters:
    case MVM_OP_typeparameterized: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_typeparameterat: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMint16 idx = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_parameterizetype: {
        MVMint16 dst    = ins->operands[0].reg.orig;
        MVMint16 type   = ins->operands[1].reg.orig;
        MVMint16 params = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { params } },
                                 { MVM_JIT_REG_ADDR, { dst } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_setparameterizer: {
        MVMint16 type        = ins->operands[0].reg.orig;
        MVMint16 parametizer = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { parametizer } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_objectid: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
        /* native references (as simple function calls for now) */
    case MVM_OP_iscont_i:
    case MVM_OP_iscont_n:
    case MVM_OP_iscont_s:
    case MVM_OP_isrwcont: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_getrusage: {
        MVMint16 obj = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_threadlockcount: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] =  { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                  { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, MVM_thread_lock_count, 2, args, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_cpucores:
    case MVM_OP_freemem:
    case MVM_OP_totalmem: {
        MVMint16 dst = ins->operands[0].reg.orig;
        jg_append_call_c(tc, jg, op_to_func(tc, op), 0, NULL, MVM_JIT_RV_INT, dst);
        break;
    }
    case MVM_OP_getsignals: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] =  { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_sleep: {
        MVMint16 time = ins->operands[0].reg.orig;
        MVMJitCallArg block_args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        MVMJitCallArg sleep_args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                       { MVM_JIT_REG_VAL_F, { time } } };
        jg_append_call_c(tc, jg, MVM_gc_mark_thread_blocked, 1, block_args, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, sleep_args, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, MVM_gc_mark_thread_unblocked, 1, block_args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_currentthread: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, MVM_thread_current, 1, args, MVM_JIT_RV_PTR, ins->operands[0].reg.orig);
        break;
    }
    case MVM_OP_getlexref_i:
    case MVM_OP_getlexref_i32:
    case MVM_OP_getlexref_i16:
    case MVM_OP_getlexref_i8:
    case MVM_OP_getlexref_u:
    case MVM_OP_getlexref_u32:
    case MVM_OP_getlexref_u16:
    case MVM_OP_getlexref_u8:
    case MVM_OP_getlexref_n:
    case MVM_OP_getlexref_n32:
    case MVM_OP_getlexref_s: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMuint16 outers = ins->operands[1].lex.outers;
        MVMuint16 idx    = ins->operands[1].lex.idx;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { outers } },
                                 { MVM_JIT_LITERAL, { idx } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getattrref_i:
    case MVM_OP_getattrref_u:
    case MVM_OP_getattrref_n:
    case MVM_OP_getattrref_s: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 obj     = ins->operands[1].reg.orig;
        MVMint16 class   = ins->operands[2].reg.orig;
        MVMint16 name    = ins->operands[3].lit_str_idx;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { class } },
                                 { MVM_JIT_STR_IDX, { name } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_getattrsref_i:
    case MVM_OP_getattrsref_n:
    case MVM_OP_getattrsref_s: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 obj     = ins->operands[1].reg.orig;
        MVMint16 class   = ins->operands[2].reg.orig;
        MVMint16 name    = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { class } },
                                 { MVM_JIT_REG_VAL, { name } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_atposref_i:
    case MVM_OP_atposref_u:
    case MVM_OP_atposref_n:
    case MVM_OP_atposref_s: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 obj     = ins->operands[1].reg.orig;
        MVMint16 index   = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { index } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
        /* profiling */
    case MVM_OP_prof_allocated: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_prof_exit: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args, MVM_JIT_RV_VOID, -1);
        break;
    }
        /* special jumplist branch */
    case MVM_OP_jumplist: {
        return consume_jumplist(tc, jg, iter, ins);
    }
        /* returning */
    case MVM_OP_return: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL, { 0 } }};
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        jg_append_call_c(tc, jg, &MVM_frame_try_return, 1, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_return_o:
    case MVM_OP_return_s:
    case MVM_OP_return_n:
    case MVM_OP_return_u:
    case MVM_OP_return_i: {
        MVMint16 reg = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = {{ MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { reg } },
                                 { MVM_JIT_LITERAL, { 0 } } };
        if (op == MVM_OP_return_n) {
            args[1].type = MVM_JIT_REG_VAL_F;
        }
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        /* reuse args for tc arg */
        jg_append_call_c(tc, jg, &MVM_frame_try_return, 1, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_sp_guard:
    case MVM_OP_sp_guardconc:
    case MVM_OP_sp_guardtype:
    case MVM_OP_sp_guardobj:
    case MVM_OP_sp_guardnotobj:
    case MVM_OP_sp_guardhll:
    case MVM_OP_sp_rebless:
        jg_append_guard(tc, jg, ins, 3);
        break;
    case MVM_OP_sp_guardjustconc:
    case MVM_OP_sp_guardjusttype:
    case MVM_OP_sp_guardsf:
    case MVM_OP_sp_guardnonzero:
        jg_append_guard(tc, jg, ins, 2);
        break;
    case MVM_OP_getexcategory: {
        MVMint16 dst     = ins->operands[0].reg.orig;
        MVMint16 obj     = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_bindexcategory: {
        MVMint16 obj      = ins->operands[0].reg.orig;
        MVMint16 category = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } },
                                 { MVM_JIT_REG_VAL, { category } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_exreturnafterunwind: {
        MVMint16 obj      = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_sp_getstringfrom: {
        MVMint16 spesh_idx = ins->operands[1].lit_i16;
        MVMuint32 cu_idx = ins->operands[2].lit_str_idx;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_SPESH_SLOT_VALUE, { spesh_idx } },
                                 { MVM_JIT_LITERAL, cu_idx } };
        jg_append_call_c(tc, jg, MVM_cu_string, 3, args, MVM_JIT_RV_PTR, ins->operands[0].reg.orig);
        break;
    }
    case MVM_OP_encoderepconf: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 str = ins->operands[1].reg.orig;
        MVMint16 encoding = ins->operands[2].reg.orig;
        MVMint16 replacement = ins->operands[3].reg.orig;
        MVMint16 blob = ins->operands[4].reg.orig;
        MVMint16 config = ins->operands[5].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { str } },
                                 { MVM_JIT_REG_VAL, { encoding } },
                                 { MVM_JIT_REG_VAL, { blob } },
                                 { MVM_JIT_REG_VAL, { replacement } },
                                 { MVM_JIT_REG_VAL, { config } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 6, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decodeconf: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 blob = ins->operands[1].reg.orig;
        MVMint16 encoding = ins->operands[2].reg.orig;
        MVMint16 config = ins->operands[3].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { blob } },
                                 { MVM_JIT_REG_VAL, { encoding } },
                                 { MVM_JIT_LITERAL_PTR, { 0 } },
                                 { MVM_JIT_REG_VAL, { config } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_decoderepconf: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 blob = ins->operands[1].reg.orig;
        MVMint16 encoding = ins->operands[2].reg.orig;
        MVMint16 replacement = ins->operands[3].reg.orig;
        MVMint16 config = ins->operands[4].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { blob } },
                                 { MVM_JIT_REG_VAL, { encoding } },
                                 { MVM_JIT_REG_VAL, { replacement } },
                                 { MVM_JIT_REG_VAL, { config } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 5, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_backtrace: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_backtracestrings: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 obj = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_REG_VAL, obj } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_breakpoint: {
        MVMint32 file_idx = ins->operands[0].lit_i16;
        MVMint32 line_no  = ins->operands[1].lit_i16;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, MVM_JIT_INTERP_TC },
                                 { MVM_JIT_LITERAL, file_idx },
                                 { MVM_JIT_LITERAL, line_no } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_strfromname: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_strfromcodes: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 src = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { src } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_callercode: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_sp_runbytecode_v:
    case MVM_OP_sp_runbytecode_i:
    case MVM_OP_sp_runbytecode_s:
    case MVM_OP_sp_runbytecode_n:
    case MVM_OP_sp_runbytecode_o: {
        int start = (op == MVM_OP_sp_runbytecode_v) ? 0 : 1;
        MVMint16 dst          = ins->operands[0].reg.orig;
        MVMint16 code         = ins->operands[0 + start].reg.orig;
        MVMCallsite *callsite = (MVMCallsite*)ins->operands[1 + start].lit_ui64;
        MVMint16 spesh_cand   = ins->operands[2 + start].lit_i16;

        /* get label /after/ current (invoke) ins, where we'll need to reenter the JIT */
        MVMint32 reentry_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
        MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
        node->type                          = MVM_JIT_NODE_RUNBYTECODE;
        node->u.runbytecode.callsite        = callsite;
        node->u.runbytecode.return_type     =
            op == MVM_OP_sp_runbytecode_v
                ? MVM_RETURN_VOID
                : op == MVM_OP_sp_runbytecode_i
                    ? MVM_RETURN_INT
                    : op == MVM_OP_sp_runbytecode_s
                        ? MVM_RETURN_STR
                        : op == MVM_OP_sp_runbytecode_n
                            ? MVM_RETURN_NUM
                            : MVM_RETURN_OBJ;
        node->u.runbytecode.return_register = dst;
        node->u.runbytecode.map             = &ins->operands[3 + start];
        node->u.runbytecode.code_register   = code;
        node->u.runbytecode.spesh_cand      = spesh_cand;
        node->u.runbytecode.reentry_label   = reentry_label;
        jg_append_node(jg, node);
        /* append reentry label */
        jg_append_label(tc, jg, reentry_label);
        break;
    }
    case MVM_OP_sp_runcfunc_v:
    case MVM_OP_sp_runcfunc_i:
    case MVM_OP_sp_runcfunc_s:
    case MVM_OP_sp_runcfunc_n:
    case MVM_OP_sp_runcfunc_o: {
        int start = (op == MVM_OP_sp_runcfunc_v) ? 0 : 1;
        MVMint16 dst          = ins->operands[0].reg.orig;
        MVMint16 code         = ins->operands[0 + start].reg.orig;
        MVMCallsite *callsite = (MVMCallsite*)ins->operands[1 + start].lit_ui64;

        /* get label /after/ current (invoke) ins, where we'll need to reenter the JIT */
        MVMint32 reentry_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
        MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
        node->type                       = MVM_JIT_NODE_RUNCCODE;
        node->u.runccode.callsite        = callsite;
        node->u.runccode.return_type     =
            op == MVM_OP_sp_runcfunc_v
                ? MVM_RETURN_VOID
                : op == MVM_OP_sp_runcfunc_i
                    ? MVM_RETURN_INT
                    : op == MVM_OP_sp_runcfunc_s
                        ? MVM_RETURN_STR
                        : op == MVM_OP_sp_runcfunc_n
                            ? MVM_RETURN_NUM
                            : MVM_RETURN_OBJ;
        node->u.runccode.return_register = dst;
        node->u.runccode.code_register   = code;
        node->u.runccode.map             = &ins->operands[2 + start];
        node->u.runccode.reentry_label   = reentry_label;
        jg_append_node(jg, node);
        /* append reentry label */
        jg_append_label(tc, jg, reentry_label);
        break;
    }
    case MVM_OP_sp_runnativecall_v:
    case MVM_OP_sp_runnativecall_i: {
        int start = (op == MVM_OP_sp_runnativecall_v) ? 0 : 1;
        MVMint16 dst          = ins->operands[0].reg.orig;

        MVMSpeshFacts *object_facts = MVM_spesh_get_facts(tc, iter->graph, ins->operands[start]);
        if (!(object_facts->flags & MVM_SPESH_FACT_KNOWN_VALUE)) {
            MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                "BAIL: op <%s> (Can't find nc_site value on spesh ins)", ins->info->name);
            return 0;
        }

        MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, object_facts->value.o);
        MVMCallsite *callsite = (MVMCallsite*)ins->operands[1 + start].lit_ui64;
        MVMJitCallArg *args = callsite->flag_count > 1 /* first arg is return_type */
            ? MVM_spesh_alloc(tc, jg->sg, (callsite->flag_count - 1) * sizeof(MVMJitCallArg))
            : NULL;

        for (int i = 1; i < callsite->flag_count; i++) {
            if ((body->arg_types[i - 1] & MVM_NATIVECALL_ARG_RW_MASK) == MVM_NATIVECALL_ARG_RW) {
                args[i - 1].type = MVM_JIT_REG_ADDR;
                args[i - 1].v.reg = ins->operands[start + 2 + i].reg.orig;
            }
            else {
                if (callsite->arg_flags[i] & (MVM_CALLSITE_ARG_INT | MVM_CALLSITE_ARG_UINT)) {
                    args[i - 1].type = MVM_JIT_REG_VAL;
                    args[i - 1].v.reg = ins->operands[start + 2 + i].reg.orig;
                }
                else if (callsite->arg_flags[i] & MVM_CALLSITE_ARG_NUM && body->arg_types[i - 1] == MVM_NATIVECALL_ARG_DOUBLE) {
                    args[i - 1].type = MVM_JIT_REG_VAL_F;
                    args[i - 1].v.reg = ins->operands[start + 2 + i].reg.orig;
                }
                else if (callsite->arg_flags[i] & MVM_CALLSITE_ARG_OBJ && body->arg_types[i - 1] == MVM_NATIVECALL_ARG_VMARRAY) {
                    args[i - 1].type = MVM_JIT_PARAM_VMARRAY;
                    args[i - 1].v.lit_i64 = ins->operands[start + 2 + i].reg.orig;
                }
                else if (callsite->arg_flags[i] & MVM_CALLSITE_ARG_OBJ && body->arg_types[i - 1] == MVM_NATIVECALL_ARG_CARRAY) {
                    args[i - 1].type = MVM_JIT_PARAM_CARRAY;
                    args[i - 1].v.lit_i64 = ins->operands[start + 2 + i].reg.orig;
                }
                else {
                    MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                        "BAIL: op <%s> (type of arg %d (%d, function's %d) NYI)", ins->info->name, i, callsite->arg_flags[i], body->arg_types[i - 1]);
                    return 0;
                }
            }
        }

        MVMint32 reentry_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
        MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
        node->type                       = MVM_JIT_NODE_RUNNATIVECALL;
        node->u.runnativecall.args            = args;
        node->u.runnativecall.num_args        = callsite->flag_count - 1;
        node->u.runnativecall.return_type     =
            op == MVM_OP_sp_runnativecall_v
                ? MVM_RETURN_VOID
                : op == MVM_OP_sp_runnativecall_i
                    ? MVM_RETURN_INT
                    : op == MVM_OP_sp_runnativecall_u
                        ? MVM_RETURN_UINT
                        : op == MVM_OP_sp_runnativecall_s
                            ? MVM_RETURN_STR
                            : op == MVM_OP_sp_runnativecall_n
                                ? MVM_RETURN_NUM
                                : MVM_RETURN_OBJ;
        node->u.runnativecall.return_register = dst;
        node->u.runnativecall.rv_type         = body->ret_type;
        node->u.runnativecall.entry_point     = body->entry_point;
        node->u.runnativecall.map             = &ins->operands[2 + start];
        node->u.runnativecall.reentry_label   = reentry_label;
        jg_append_node(jg, node);
        /* append reentry label */
        jg_append_label(tc, jg, reentry_label);
        /* Check the entry_point after we've used it to generate the code to avoid a race
         * condition where the check turned out fine, but entry_point got overwritten before we
         * got to use it for generating the code. */
        if (!body->entry_point) {
            MVM_spesh_graph_add_comment(tc, iter->graph, iter->ins,
                "BAIL: op <%s> (entry_point is NULL)", ins->info->name);
            return 0;
        }
        break;
    }
    case MVM_OP_sp_dispatch_v:
    case MVM_OP_sp_dispatch_i:
    case MVM_OP_sp_dispatch_u:
    case MVM_OP_sp_dispatch_s:
    case MVM_OP_sp_dispatch_n:
    case MVM_OP_sp_dispatch_o: {
        int start = (op == MVM_OP_sp_dispatch_v) ? 0 : 1;
        MVMint16 dst          = ins->operands[0].reg.orig;
        MVMint32 id           = ins->operands[0 + start].lit_ui32;
        MVMCallsite *callsite = jg->sg->sf->body.cu->body.callsites[ins->operands[1 + start].lit_ui16];
        MVMuint16 sf_slot     = ins->operands[2 + start].lit_ui16;
        MVMuint32 ice_slot    = ins->operands[3 + start].lit_ui32;

        /* get label /after/ current (dispatch) ins, where we'll need to reenter the JIT */
        MVMint32 reentry_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
        MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
        node->type                       = MVM_JIT_NODE_DISPATCH;
        node->u.dispatch.id              = id;
        node->u.dispatch.callsite        = callsite;
        node->u.dispatch.sf_slot         = sf_slot;
        node->u.dispatch.ice_slot        = ice_slot;
        node->u.dispatch.return_type     =
            op == MVM_OP_sp_dispatch_v
                ? MVM_RETURN_VOID
                : op == MVM_OP_sp_dispatch_i
                    ? MVM_RETURN_INT
                    : op == MVM_OP_sp_dispatch_u
                        ? MVM_RETURN_UINT
                        : op == MVM_OP_sp_dispatch_s
                            ? MVM_RETURN_STR
                            : op == MVM_OP_sp_dispatch_n
                                ? MVM_RETURN_NUM
                                : MVM_RETURN_OBJ;
        node->u.dispatch.return_register = dst;
        node->u.dispatch.map             = &ins->operands[4 + start];
        node->u.dispatch.reentry_label   = reentry_label;
        jg_append_node(jg, node);
        /* append reentry label */
        jg_append_label(tc, jg, reentry_label);
        break;
    }
    case MVM_OP_sp_istype: {
        /* get label /after/ current istype ins, where we'll need to reenter the JIT
         * in the case there's a call; we'll also use it to skip over the call if
         * we resolve it via the cache */
        MVMint32 reentry_label = MVM_jit_label_after_ins(tc, jg, iter->bb, ins);
        MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
        node->type                       = MVM_JIT_NODE_ISTYPE;
        node->u.istype.return_register   = ins->operands[0].reg.orig;
        node->u.istype.obj_register      = ins->operands[1].reg.orig;
        node->u.istype.type_register     = ins->operands[2].reg.orig;
        node->u.istype.sf_slot           = ins->operands[3].lit_ui16;
        node->u.istype.ice_slot          = ins->operands[4].lit_ui32;
        node->u.istype.reentry_label     = reentry_label;
        jg_append_node(jg, node);
        /* append reentry label */
        jg_append_label(tc, jg, reentry_label);
        break;
    }
    case MVM_OP_loadlib: {
        MVMint16 name = ins->operands[0].reg.orig;
        MVMint16 path = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { name } },
                                 { MVM_JIT_REG_VAL, { path } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_sha1: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 str = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { str } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 2, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_loadext: {
        MVMint16 lib = ins->operands[0].reg.orig;
        MVMint16 ext = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { lib } },
                                 { MVM_JIT_REG_VAL, { ext } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_settypefinalize: {
        MVMint16 type     = ins->operands[0].reg.orig;
        MVMint16 finalize = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { finalize } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_usecompileehllconfig:
    case MVM_OP_usecompilerhllconfig: {
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 1, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_getcurhllsym: {
        MVMint16 dst = ins->operands[0].reg.orig;
        MVMint16 sym = ins->operands[1].reg.orig;
        MVMString *hll_name = jg->sg->sf->body.cu->body.hll_name;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_LITERAL_PTR, { (uintptr_t)hll_name } },
                                 { MVM_JIT_REG_VAL, { sym } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_PTR, dst);
        break;
    }
    case MVM_OP_scsetobj:
    case MVM_OP_scsetcode: {
        MVMint16 sc  = ins->operands[0].reg.orig;
        MVMint16 idx = ins->operands[1].reg.orig;
        MVMint16 obj = ins->operands[2].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { sc } },
                                 { MVM_JIT_REG_VAL, { idx } },
                                 { MVM_JIT_REG_VAL, { obj } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 4, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    case MVM_OP_setdebugtypename: {
        MVMint16 type = ins->operands[0].reg.orig;
        MVMint16 name = ins->operands[1].reg.orig;
        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR, { MVM_JIT_INTERP_TC } },
                                 { MVM_JIT_REG_VAL, { type } },
                                 { MVM_JIT_REG_VAL, { name } } };
        jg_append_call_c(tc, jg, op_to_func(tc, op), 3, args, MVM_JIT_RV_VOID, -1);
        break;
    }
    default: {
        /* Check if it's an extop. */
        MVMint32 emitted_extop = 0;
        if (ins->info->opcode == (MVMuint16)-1) {
            MVMExtOpRecord *extops     = jg->sg->sf->body.cu->body.extops;
            MVMuint16       num_extops = jg->sg->sf->body.cu->body.num_extops;
            MVMuint16       i;
            for (i = 0; i < num_extops; i++) {
                if (extops[i].info == ins->info && !extops[i].no_jit) {
                    size_t fake_regs_size;
                    MVMuint16 *fake_regs = try_fake_extop_regs(tc, jg->sg, ins, &fake_regs_size);
                    if (fake_regs_size && fake_regs != NULL) {
                        MVMint32  data_label = jg_add_data_node(tc, jg, fake_regs, fake_regs_size);
                        MVMJitCallArg args[] = { { MVM_JIT_INTERP_VAR,  { MVM_JIT_INTERP_TC } },
                                                 { MVM_JIT_DATA_LABEL,  { data_label } }};
                        jg_append_call_c(tc, jg, extops[i].func, 2, args, MVM_JIT_RV_VOID, -1);
                        emitted_extop = 1;
                    }
                    break;
                }
            }
        }
        if (!emitted_extop) {
            add_bail_comment(tc, jg, ins);
            return 0;
        }
    }
    }
    return 1;
}

static MVMint32 consume_bb(MVMThreadContext *tc, MVMJitGraph *jg,
                           MVMSpeshIterator *iter, MVMSpeshBB *bb) {
    MVMJitExprTree *tree = NULL;
    MVMuint32 i;
    MVMint32 label = MVM_jit_label_before_bb(tc, jg, bb);
    jg_append_label(tc, jg, label);

    /* add a jit breakpoint if required */
    for (i = 0; i < tc->instance->jit_breakpoints_num; i++) {
        if (tc->instance->jit_breakpoints[i].frame_nr == tc->instance->spesh_produced &&
            tc->instance->jit_breakpoints[i].block_nr == iter->bb->idx) {
            jg_append_control(tc, jg, bb->first_ins, MVM_JIT_CONTROL_BREAKPOINT);
            break; /* one is enough though */
        }
    }

    /* Try to create an expression tree */
    if (tc->instance->jit_expr_enabled &&
        (tc->instance->jit_expr_last_frame < 0 ||
         tc->instance->spesh_produced < tc->instance->jit_expr_last_frame ||
         (tc->instance->spesh_produced == tc->instance->jit_expr_last_frame &&
          (tc->instance->jit_expr_last_bb < 0 ||
           iter->bb->idx <= tc->instance->jit_expr_last_bb)))) {

        while (iter->ins) {
            /* consumes iterator */
            tree = MVM_jit_expr_tree_build(tc, jg, iter);
            if (tree != NULL) {
                MVMJitNode *node = MVM_spesh_alloc(tc, jg->sg, sizeof(MVMJitNode));
                node->type       = MVM_JIT_NODE_EXPR_TREE;
                node->u.tree     = tree;
                tree->seq_nr     = jg->expr_seq_nr++;
                jg_append_node(jg, node);
            }
            if (iter->ins) {
                /* something we can't compile yet, or simply an empty tree */
                break;
            }
        }
    }

    /* Try to consume the (rest of the) basic block per instruction */
    while (iter->ins) {
        before_ins(tc, jg, iter, iter->ins);
        if(!consume_ins(tc, jg, iter, iter->ins))
            return 0;
        after_ins(tc, jg, iter, iter->ins);
        MVM_spesh_iterator_next_ins(tc, iter);
    }

    return 1;
}


MVMJitGraph * MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg) {
    MVMSpeshIterator iter;
    MVMJitGraph *graph;

    if (!MVM_jit_support()) {
        return NULL;
    }

    MVM_spesh_iterator_init(tc, &iter, sg);
    /* ignore first BB, which always contains a NOP */
    MVM_spesh_iterator_next_bb(tc, &iter);

    graph             = MVM_spesh_alloc(tc, sg, sizeof(MVMJitGraph));
    graph->sg         = sg;
    graph->first_node = NULL;
    graph->last_node  = NULL;

    /* Set initial instruction label offset */
    graph->obj_label_ofs = sg->num_bbs + 1;

    /* Labels for individual instructions (not basic blocks), for instance at
     * boundaries of exception handling frames */
    MVM_VECTOR_INIT(graph->obj_labels, 16);

    /* Deoptimization labels */
    MVM_VECTOR_INIT(graph->deopts, 8);
    /* Nodes for each label, used to ensure labels aren't added twice */
    MVM_VECTOR_INIT(graph->label_nodes, 16 + sg->num_bbs);

    graph->expr_seq_nr = 0;

    /* JIT handlers are indexed by spesh graph handler index */
    if (sg->num_handlers > 0) {
        MVM_VECTOR_INIT(graph->handlers, sg->num_handlers);
        graph->handlers_num = sg->num_handlers;
    } else {
        graph->handlers     = NULL;
        graph->handlers_num = 0;
    }

    /* JIT inlines are indexed by spesh graph inline index */
    if (sg->num_inlines > 0) {
        MVM_VECTOR_INIT(graph->inlines, sg->num_inlines);
        graph->inlines_num = sg->num_inlines;
    } else {
        graph->inlines     = NULL;
        graph->inlines_num = 0;
    }

    /* Add start-of-graph label */
    jg_append_label(tc, graph, MVM_jit_label_before_graph(tc, graph, sg));
    /* Loop over basic blocks */
    while (iter.bb) {
        if (!consume_bb(tc, graph, &iter, iter.bb))
            goto bail;
        MVM_spesh_iterator_next_bb(tc, &iter);
    }
    /* Check if we've added a instruction at all */
    if (!graph->first_node)
        goto bail;

    /* append the end-of-graph label */
    jg_append_label(tc, graph, MVM_jit_label_after_graph(tc, graph, sg));

    /* Calculate number of basic block + graph labels */
    graph->num_labels    = graph->obj_label_ofs + graph->obj_labels_num;

    return graph;

 bail:
    MVM_jit_graph_destroy(tc, graph);
    return NULL;
}

void MVM_jit_graph_destroy(MVMThreadContext *tc, MVMJitGraph *graph) {
    MVMJitNode *node;
    /* destroy all trees */
    for (node = graph->first_node; node != NULL; node = node->next) {
        if (node->type == MVM_JIT_NODE_EXPR_TREE) {
            MVM_jit_expr_tree_destroy(tc, node->u.tree);
        }
    }
    MVM_free(graph->label_nodes);
    MVM_free(graph->obj_labels);
    MVM_free(graph->deopts);
    MVM_free(graph->handlers);
    MVM_free(graph->inlines);
}
