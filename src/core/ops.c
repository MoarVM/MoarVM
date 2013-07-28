#ifdef PARROT_OPS_BUILD
#define PARROT_IN_EXTENSION
#include "parrot/parrot.h"
#include "parrot/extend.h"
#include "sixmodelobject.h"
#include "nodes_parrot.h"
#include "../../src/core/ops.h"
#else
#include "moarvm.h"
#endif
/* This file is generated from src/core/oplist by tools/update_ops.p6. */
static MVMOpInfo MVM_op_info_primitives[] = {
    {
        MVM_OP_no_op,
        "no_op",
        0,
    },
    {
        MVM_OP_goto,
        "goto",
        1,
        { MVM_operand_ins }
    },
    {
        MVM_OP_if_i,
        "if_i",
        2,
        { MVM_operand_read_reg | MVM_operand_int64, MVM_operand_ins }
    },
    {
        MVM_OP_unless_i,
        "unless_i",
        2,
        { MVM_operand_read_reg | MVM_operand_int64, MVM_operand_ins }
    },
    {
        MVM_OP_if_n,
        "if_n",
        2,
        { MVM_operand_read_reg | MVM_operand_num64, MVM_operand_ins }
    },
    {
        MVM_OP_unless_n,
        "unless_n",
        2,
        { MVM_operand_read_reg | MVM_operand_num64, MVM_operand_ins }
    },
    {
        MVM_OP_if_s,
        "if_s",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_unless_s,
        "unless_s",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_if_s0,
        "if_s0",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_unless_s0,
        "unless_s0",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_if_o,
        "if_o",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_ins }
    },
    {
        MVM_OP_unless_o,
        "unless_o",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_ins }
    },
    {
        MVM_OP_set,
        "set",
        2,
        { MVM_operand_write_reg | MVM_operand_type_var, MVM_operand_read_reg | MVM_operand_type_var }
    },
    {
        MVM_OP_extend_u8,
        "extend_u8",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int8 }
    },
    {
        MVM_OP_extend_u16,
        "extend_u16",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int16 }
    },
    {
        MVM_OP_extend_u32,
        "extend_u32",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int32 }
    },
    {
        MVM_OP_extend_i8,
        "extend_i8",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int8 }
    },
    {
        MVM_OP_extend_i16,
        "extend_i16",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int16 }
    },
    {
        MVM_OP_extend_i32,
        "extend_i32",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int32 }
    },
    {
        MVM_OP_trunc_u8,
        "trunc_u8",
        2,
        { MVM_operand_write_reg | MVM_operand_int8, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_trunc_u16,
        "trunc_u16",
        2,
        { MVM_operand_write_reg | MVM_operand_int16, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_trunc_u32,
        "trunc_u32",
        2,
        { MVM_operand_write_reg | MVM_operand_int32, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_trunc_i8,
        "trunc_i8",
        2,
        { MVM_operand_write_reg | MVM_operand_int8, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_trunc_i16,
        "trunc_i16",
        2,
        { MVM_operand_write_reg | MVM_operand_int16, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_trunc_i32,
        "trunc_i32",
        2,
        { MVM_operand_write_reg | MVM_operand_int32, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_extend_n32,
        "extend_n32",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num32 }
    },
    {
        MVM_OP_trunc_n32,
        "trunc_n32",
        2,
        { MVM_operand_write_reg | MVM_operand_num32, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_getlex,
        "getlex",
        2,
        { MVM_operand_write_reg | MVM_operand_type_var, MVM_operand_read_lex | MVM_operand_type_var }
    },
    {
        MVM_OP_bindlex,
        "bindlex",
        2,
        { MVM_operand_write_lex | MVM_operand_type_var, MVM_operand_read_reg | MVM_operand_type_var }
    },
    {
        MVM_OP_getlex_ni,
        "getlex_ni",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_str }
    },
    {
        MVM_OP_getlex_nn,
        "getlex_nn",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_str }
    },
    {
        MVM_OP_getlex_ns,
        "getlex_ns",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_str }
    },
    {
        MVM_OP_getlex_no,
        "getlex_no",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_str }
    },
    {
        MVM_OP_bindlex_ni,
        "bindlex_ni",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bindlex_nn,
        "bindlex_nn",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_bindlex_ns,
        "bindlex_ns",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindlex_no,
        "bindlex_no",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getlex_ng,
        "getlex_ng",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindlex_ng,
        "bindlex_ng",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_return_i,
        "return_i",
        1,
        { MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_return_n,
        "return_n",
        1,
        { MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_return_s,
        "return_s",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_return_o,
        "return_o",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_return,
        "return",
        0,
    },
    {
        MVM_OP_const_i8,
        "const_i8",
        2,
        { MVM_operand_write_reg | MVM_operand_int8, MVM_operand_int8 }
    },
    {
        MVM_OP_const_i16,
        "const_i16",
        2,
        { MVM_operand_write_reg | MVM_operand_int16, MVM_operand_int16 }
    },
    {
        MVM_OP_const_i32,
        "const_i32",
        2,
        { MVM_operand_write_reg | MVM_operand_int32, MVM_operand_int32 }
    },
    {
        MVM_OP_const_i64,
        "const_i64",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_int64 }
    },
    {
        MVM_OP_const_n32,
        "const_n32",
        2,
        { MVM_operand_write_reg | MVM_operand_num32, MVM_operand_num32 }
    },
    {
        MVM_OP_const_n64,
        "const_n64",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_num64 }
    },
    {
        MVM_OP_const_s,
        "const_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_str }
    },
    {
        MVM_OP_add_i,
        "add_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_sub_i,
        "sub_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_mul_i,
        "mul_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_div_i,
        "div_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_div_u,
        "div_u",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_mod_i,
        "mod_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_mod_u,
        "mod_u",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_neg_i,
        "neg_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_abs_i,
        "abs_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_inc_i,
        "inc_i",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_inc_u,
        "inc_u",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_dec_i,
        "dec_i",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_dec_u,
        "dec_u",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getcode,
        "getcode",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_coderef }
    },
    {
        MVM_OP_prepargs,
        "prepargs",
        1,
        { MVM_operand_callsite }
    },
    {
        MVM_OP_arg_i,
        "arg_i",
        2,
        { MVM_operand_int16, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_arg_n,
        "arg_n",
        2,
        { MVM_operand_int16, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_arg_s,
        "arg_s",
        2,
        { MVM_operand_int16, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_arg_o,
        "arg_o",
        2,
        { MVM_operand_int16, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_invoke_v,
        "invoke_v",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_invoke_i,
        "invoke_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_invoke_n,
        "invoke_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_invoke_s,
        "invoke_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_invoke_o,
        "invoke_o",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_add_n,
        "add_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_sub_n,
        "sub_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_mul_n,
        "mul_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_div_n,
        "div_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_neg_n,
        "neg_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_abs_n,
        "abs_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_eq_i,
        "eq_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_ne_i,
        "ne_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_lt_i,
        "lt_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_le_i,
        "le_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_gt_i,
        "gt_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_ge_i,
        "ge_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_eq_n,
        "eq_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_ne_n,
        "ne_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_lt_n,
        "lt_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_le_n,
        "le_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_gt_n,
        "gt_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_ge_n,
        "ge_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_argconst_i,
        "argconst_i",
        2,
        { MVM_operand_int16, MVM_operand_int64 }
    },
    {
        MVM_OP_argconst_n,
        "argconst_n",
        2,
        { MVM_operand_int16, MVM_operand_num64 }
    },
    {
        MVM_OP_argconst_s,
        "argconst_s",
        2,
        { MVM_operand_int16, MVM_operand_str }
    },
    {
        MVM_OP_checkarity,
        "checkarity",
        2,
        { MVM_operand_int16, MVM_operand_int16 }
    },
    {
        MVM_OP_param_rp_i,
        "param_rp_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_int16 }
    },
    {
        MVM_OP_param_rp_n,
        "param_rp_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_int16 }
    },
    {
        MVM_OP_param_rp_s,
        "param_rp_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_param_rp_o,
        "param_rp_o",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int16 }
    },
    {
        MVM_OP_param_op_i,
        "param_op_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_int16, MVM_operand_ins }
    },
    {
        MVM_OP_param_op_n,
        "param_op_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_int16, MVM_operand_ins }
    },
    {
        MVM_OP_param_op_s,
        "param_op_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_int16, MVM_operand_ins }
    },
    {
        MVM_OP_param_op_o,
        "param_op_o",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int16, MVM_operand_ins }
    },
    {
        MVM_OP_param_rn_i,
        "param_rn_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_str }
    },
    {
        MVM_OP_param_rn_n,
        "param_rn_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_str }
    },
    {
        MVM_OP_param_rn_s,
        "param_rn_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_str }
    },
    {
        MVM_OP_param_rn_o,
        "param_rn_o",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_str }
    },
    {
        MVM_OP_param_on_i,
        "param_on_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_param_on_n,
        "param_on_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_param_on_s,
        "param_on_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_param_on_o,
        "param_on_o",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_coerce_in,
        "coerce_in",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_coerce_ni,
        "coerce_ni",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_band_i,
        "band_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bor_i,
        "bor_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bxor_i,
        "bxor_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bnot_i,
        "bnot_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_blshift_i,
        "blshift_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_brshift_i,
        "brshift_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_pow_i,
        "pow_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_pow_n,
        "pow_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_takeclosure,
        "takeclosure",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_jumplist,
        "jumplist",
        2,
        { MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_caller,
        "caller",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getdynlex,
        "getdynlex",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_binddynlex,
        "binddynlex",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_is,
        "coerce_is",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_coerce_ns,
        "coerce_ns",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_coerce_si,
        "coerce_si",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_coerce_sn,
        "coerce_sn",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_smrt_numify,
        "smrt_numify",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_smrt_strify,
        "smrt_strify",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_param_sp,
        "param_sp",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int16 }
    },
    {
        MVM_OP_param_sn,
        "param_sn",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ifnonnull,
        "ifnonnull",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_ins }
    },
    {
        MVM_OP_cmp_i,
        "cmp_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_cmp_n,
        "cmp_n",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_not_i,
        "not_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_setlexvalue,
        "setlexvalue",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_int16 }
    },
    {
        MVM_OP_exception,
        "exception",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_handled,
        "handled",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_newexception,
        "newexception",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bindexmessage,
        "bindexmessage",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindexpayload,
        "bindexpayload",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bindexcategory,
        "bindexcategory",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getexmessage,
        "getexmessage",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getexpayload,
        "getexpayload",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getexcategory,
        "getexcategory",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_throwdyn,
        "throwdyn",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_throwlex,
        "throwlex",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_throwlexotic,
        "throwlexotic",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_throwcatdyn,
        "throwcatdyn",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int64 }
    },
    {
        MVM_OP_throwcatlex,
        "throwcatlex",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int64 }
    },
    {
        MVM_OP_throwcatlexotic,
        "throwcatlexotic",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int64 }
    },
    {
        MVM_OP_die,
        "die",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_newlexotic,
        "newlexotic",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_ins }
    },
    {
        MVM_OP_lexoticresult,
        "lexoticresult",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_mod_n,
        "mod_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_usecapture,
        "usecapture",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_savecapture,
        "savecapture",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_captureposelems,
        "captureposelems",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_captureposarg,
        "captureposarg",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_captureposarg_i,
        "captureposarg_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_captureposarg_n,
        "captureposarg_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_captureposarg_s,
        "captureposarg_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_captureposprimspec,
        "captureposprimspec",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_invokewithcapture,
        "invokewithcapture",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_multicacheadd,
        "multicacheadd",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_multicachefind,
        "multicachefind",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_lexprimspec,
        "lexprimspec",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_ceil_n,
        "ceil_n",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_floor_n,
        "floor_n",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_assign,
        "assign",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_assignunchecked,
        "assignunchecked",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_objprimspec,
        "objprimspec",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_backtracestrings,
        "backtracestrings",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
};
static MVMOpInfo MVM_op_info_dev[] = {
    {
        MVM_OP_sleep,
        "sleep",
        1,
        { MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_say_I,
        "say_I",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
};
static MVMOpInfo MVM_op_info_string[] = {
    {
        MVM_OP_concat_s,
        "concat_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_repeat_s,
        "repeat_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_substr_s,
        "substr_s",
        4,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_index_s,
        "index_s",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_graphs_s,
        "graphs_s",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_codes_s,
        "codes_s",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_eq_s,
        "eq_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_ne_s,
        "ne_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_eqat_s,
        "eqat_s",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_haveat_s,
        "haveat_s",
        6,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getcp_s,
        "getcp_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_indexcp_s,
        "indexcp_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_uc,
        "uc",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_lc,
        "lc",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_tc,
        "tc",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_buftostr,
        "buftostr",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_strtobuf,
        "strtobuf",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_decode_s,
        "decode_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_decode_b,
        "decode_b",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_decode,
        "decode",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_encode,
        "encode",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_split,
        "split",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_join,
        "join",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_replace,
        "replace",
        4,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getcpbyname,
        "getcpbyname",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_indexat_scb,
        "indexat_scb",
        4,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_str, MVM_operand_ins }
    },
    {
        MVM_OP_unipropcode,
        "unipropcode",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_unipvalcode,
        "unipvalcode",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_hasuniprop,
        "hasuniprop",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_hasunipropc,
        "hasunipropc",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_int16, MVM_operand_int16 }
    },
    {
        MVM_OP_concatr_s,
        "concatr_s",
        7,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_splice_s,
        "splice_s",
        7,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_chars,
        "chars",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_chr,
        "chr",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_ordfirst,
        "ordfirst",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_ordat,
        "ordat",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_rindexfrom,
        "rindexfrom",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_escape,
        "escape",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_flip,
        "flip",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_iscclass,
        "iscclass",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_findcclass,
        "findcclass",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_findnotcclass,
        "findnotcclass",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_nfafromstatelist,
        "nfafromstatelist",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_nfarunproto,
        "nfarunproto",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_nfarunalt,
        "nfarunalt",
        6,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_flattenropes,
        "flattenropes",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_gt_s,
        "gt_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_ge_s,
        "ge_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_lt_s,
        "lt_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_le_s,
        "le_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_cmp_s,
        "cmp_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_radix,
        "radix",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_eqatic_s,
        "eqatic_s",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
};
static MVMOpInfo MVM_op_info_math[] = {
    {
        MVM_OP_sin_n,
        "sin_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_asin_n,
        "asin_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_cos_n,
        "cos_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_acos_n,
        "acos_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_tan_n,
        "tan_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_atan_n,
        "atan_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_atan2_n,
        "atan2_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_write_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_sec_n,
        "sec_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_asec_n,
        "asec_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_sinh_n,
        "sinh_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_cosh_n,
        "cosh_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_tanh_n,
        "tanh_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_sech_n,
        "sech_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_sqrt_n,
        "sqrt_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_gcd_i,
        "gcd_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_lcm_i,
        "lcm_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_add_I,
        "add_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_sub_I,
        "sub_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_mul_I,
        "mul_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_div_I,
        "div_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_mod_I,
        "mod_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_neg_I,
        "neg_I",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_abs_I,
        "abs_I",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_inc_I,
        "inc_I",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_dec_I,
        "dec_I",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_cmp_I,
        "cmp_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_eq_I,
        "eq_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ne_I,
        "ne_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_lt_I,
        "lt_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_le_I,
        "le_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_gt_I,
        "gt_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ge_I,
        "ge_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_not_I,
        "not_I",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bor_I,
        "bor_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bxor_I,
        "bxor_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_band_I,
        "band_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bnot_I,
        "bnot_I",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_blshift_I,
        "blshift_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_brshift_I,
        "brshift_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pow_I,
        "pow_I",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_gcd_I,
        "gcd_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_lcm_I,
        "lcm_I",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_expmod_I,
        "expmod_I",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isprime_I,
        "isprime_I",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_rand_I,
        "rand_I",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_Ii,
        "coerce_Ii",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_In,
        "coerce_In",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_Is,
        "coerce_Is",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_iI,
        "coerce_iI",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_nI,
        "coerce_nI",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_coerce_sI,
        "coerce_sI",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isbig_I,
        "isbig_I",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_base_I,
        "base_I",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_radix_I,
        "radix_I",
        6,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_div_In,
        "div_In",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_log_n,
        "log_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_exp_n,
        "exp_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_num64 }
    },
};
static MVMOpInfo MVM_op_info_object[] = {
    {
        MVM_OP_knowhow,
        "knowhow",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_findmeth,
        "findmeth",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str }
    },
    {
        MVM_OP_findmeth_s,
        "findmeth_s",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_can,
        "can",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str }
    },
    {
        MVM_OP_can_s,
        "can_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_create,
        "create",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_gethow,
        "gethow",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getwhat,
        "getwhat",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_atkey_i,
        "atkey_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_atkey_n,
        "atkey_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_atkey_s,
        "atkey_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_atkey_o,
        "atkey_o",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindkey_i,
        "bindkey_i",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bindkey_n,
        "bindkey_n",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_bindkey_s,
        "bindkey_s",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindkey_o,
        "bindkey_o",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_existskey,
        "existskey",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_deletekey,
        "deletekey",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getwhere,
        "getwhere",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_eqaddr,
        "eqaddr",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_reprname,
        "reprname",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isconcrete,
        "isconcrete",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_atpos_i,
        "atpos_i",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_atpos_n,
        "atpos_n",
        3,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_atpos_s,
        "atpos_s",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_atpos_o,
        "atpos_o",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bindpos_i,
        "bindpos_i",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bindpos_n,
        "bindpos_n",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_bindpos_s,
        "bindpos_s",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindpos_o,
        "bindpos_o",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_push_i,
        "push_i",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_push_n,
        "push_n",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_push_s,
        "push_s",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_push_o,
        "push_o",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pop_i,
        "pop_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pop_n,
        "pop_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pop_s,
        "pop_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pop_o,
        "pop_o",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_shift_i,
        "shift_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_shift_n,
        "shift_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_shift_s,
        "shift_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_shift_o,
        "shift_o",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_unshift_i,
        "unshift_i",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_unshift_n,
        "unshift_n",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_unshift_s,
        "unshift_s",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_unshift_o,
        "unshift_o",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_splice,
        "splice",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP___INVALID_1__,
        "__INVALID_1__",
        0,
    },
    {
        MVM_OP_setelemspos,
        "setelemspos",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_box_i,
        "box_i",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_box_n,
        "box_n",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_box_s,
        "box_s",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_unbox_i,
        "unbox_i",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_unbox_n,
        "unbox_n",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_unbox_s,
        "unbox_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bindattr_i,
        "bindattr_i",
        5,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_int16 }
    },
    {
        MVM_OP_bindattr_n,
        "bindattr_n",
        5,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64, MVM_operand_int16 }
    },
    {
        MVM_OP_bindattr_s,
        "bindattr_s",
        5,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_bindattr_o,
        "bindattr_o",
        5,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_int16 }
    },
    {
        MVM_OP_bindattrs_i,
        "bindattrs_i",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bindattrs_n,
        "bindattrs_n",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_bindattrs_s,
        "bindattrs_s",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindattrs_o,
        "bindattrs_o",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getattr_i,
        "getattr_i",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_getattr_n,
        "getattr_n",
        5,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_getattr_s,
        "getattr_s",
        5,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_getattr_o,
        "getattr_o",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_str, MVM_operand_int16 }
    },
    {
        MVM_OP_getattrs_i,
        "getattrs_i",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getattrs_n,
        "getattrs_n",
        4,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getattrs_s,
        "getattrs_s",
        4,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getattrs_o,
        "getattrs_o",
        4,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_isnull,
        "isnull",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_knowhowattr,
        "knowhowattr",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_iscoderef,
        "iscoderef",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_null,
        "null",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_clone,
        "clone",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isnull_s,
        "isnull_s",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bootint,
        "bootint",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootnum,
        "bootnum",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootstr,
        "bootstr",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootarray,
        "bootarray",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_boothash,
        "boothash",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_sethllconfig,
        "sethllconfig",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hllboxtype_i,
        "hllboxtype_i",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hllboxtype_n,
        "hllboxtype_n",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hllboxtype_s,
        "hllboxtype_s",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_elems,
        "elems",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_null_s,
        "null_s",
        1,
        { MVM_operand_write_reg | MVM_operand_str }
    },
    {
        MVM_OP_newtype,
        "newtype",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_islist,
        "islist",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ishash,
        "ishash",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_iter,
        "iter",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_iterkey_s,
        "iterkey_s",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_iterval,
        "iterval",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getcodename,
        "getcodename",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_composetype,
        "composetype",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setmethcache,
        "setmethcache",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setmethcacheauth,
        "setmethcacheauth",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_settypecache,
        "settypecache",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setinvokespec,
        "setinvokespec",
        4,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isinvokable,
        "isinvokable",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_iscont,
        "iscont",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_decont,
        "decont",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setboolspec,
        "setboolspec",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_istrue,
        "istrue",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_isfalse,
        "isfalse",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_istrue_s,
        "istrue_s",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_isfalse_s,
        "isfalse_s",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getcodeobj,
        "getcodeobj",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setcodeobj,
        "setcodeobj",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setcodename,
        "setcodename",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_forceouterctx,
        "forceouterctx",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getcomp,
        "getcomp",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindcomp,
        "bindcomp",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getcurhllsym,
        "getcurhllsym",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bindcurhllsym,
        "bindcurhllsym",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getwho,
        "getwho",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setwho,
        "setwho",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_rebless,
        "rebless",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_istype,
        "istype",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ctx,
        "ctx",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ctxouter,
        "ctxouter",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ctxcaller,
        "ctxcaller",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_ctxlexpad,
        "ctxlexpad",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_curcode,
        "curcode",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_callercode,
        "callercode",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootintarray,
        "bootintarray",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootnumarray,
        "bootnumarray",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bootstrarray,
        "bootstrarray",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hlllist,
        "hlllist",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hllhash,
        "hllhash",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_attrinited,
        "attrinited",
        4,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_setcontspec,
        "setcontspec",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_existspos,
        "existspos",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_gethllsym,
        "gethllsym",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
};
static MVMOpInfo MVM_op_info_io[] = {
    {
        MVM_OP_copy_f,
        "copy_f",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_append_f,
        "append_f",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_rename_f,
        "rename_f",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_delete_f,
        "delete_f",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_chmod_f,
        "chmod_f",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_exists_f,
        "exists_f",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_mkdir,
        "mkdir",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_rmdir,
        "rmdir",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_open_dir,
        "open_dir",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_read_dir,
        "read_dir",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_close_dir,
        "close_dir",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_open_fh,
        "open_fh",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_close_fh,
        "close_fh",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_read_fhs,
        "read_fhs",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_read_fhbuf,
        "read_fhbuf",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_slurp,
        "slurp",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_spew,
        "spew",
        3,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_write_fhs,
        "write_fhs",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_write_fhbuf,
        "write_fhbuf",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_seek_fh,
        "seek_fh",
        3,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_lock_fh,
        "lock_fh",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_unlock_fh,
        "unlock_fh",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_flush_fh,
        "flush_fh",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_sync_fh,
        "sync_fh",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pipe_fh,
        "pipe_fh",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_trunc_fh,
        "trunc_fh",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_eof_fh,
        "eof_fh",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getstdin,
        "getstdin",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getstdout,
        "getstdout",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getstderr,
        "getstderr",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_connect_sk,
        "connect_sk",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_close_sk,
        "close_sk",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_bind_sk,
        "bind_sk",
        5,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_listen_sk,
        "listen_sk",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_accept_sk,
        "accept_sk",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_send_sks,
        "send_sks",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_send_skbuf,
        "send_skbuf",
        5,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_recv_sks,
        "recv_sks",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_recv_skbuf,
        "recv_skbuf",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getaddr_sk,
        "getaddr_sk",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_hostname,
        "hostname",
        1,
        { MVM_operand_write_reg | MVM_operand_str }
    },
    {
        MVM_OP_nametoaddr,
        "nametoaddr",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_addrtoname,
        "addrtoname",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_porttosvc,
        "porttosvc",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_setencoding,
        "setencoding",
        2,
        { MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_print,
        "print",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_say,
        "say",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_readall_fh,
        "readall_fh",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_tell_fh,
        "tell_fh",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_stat,
        "stat",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_readline_fh,
        "readline_fh",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
};
static MVMOpInfo MVM_op_info_processthread[] = {
    {
        MVM_OP_getenv,
        "getenv",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_setenv,
        "setenv",
        2,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_delenv,
        "delenv",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_nametogid,
        "nametogid",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_gidtoname,
        "gidtoname",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_nametouid,
        "nametouid",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_uidtoname,
        "uidtoname",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getusername,
        "getusername",
        1,
        { MVM_operand_write_reg | MVM_operand_str }
    },
    {
        MVM_OP_getuid,
        "getuid",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_getgid,
        "getgid",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_gethomedir,
        "gethomedir",
        1,
        { MVM_operand_write_reg | MVM_operand_str }
    },
    {
        MVM_OP_getencoding,
        "getencoding",
        1,
        { MVM_operand_write_reg | MVM_operand_str }
    },
    {
        MVM_OP_procshell,
        "procshell",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_procshellbg,
        "procshellbg",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_procrun,
        "procrun",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_procrunbg,
        "procrunbg",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_prockill,
        "prockill",
        2,
        { MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_procwait,
        "procwait",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_procalive,
        "procalive",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_detach,
        "detach",
        0,
    },
    {
        MVM_OP_daemonize,
        "daemonize",
        0,
    },
    {
        MVM_OP_chdir,
        "chdir",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_rand_i,
        "rand_i",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_rand_n,
        "rand_n",
        1,
        { MVM_operand_write_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_time_i,
        "time_i",
        1,
        { MVM_operand_write_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_clargs,
        "clargs",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_newthread,
        "newthread",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_jointhread,
        "jointhread",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_time_n,
        "time_n",
        1,
        { MVM_operand_write_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_exit,
        "exit",
        1,
        { MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_loadbytecode,
        "loadbytecode",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_getenvhash,
        "getenvhash",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
};
static MVMOpInfo MVM_op_info_serialization[] = {
    {
        MVM_OP_sha1,
        "sha1",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_createsc,
        "createsc",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_scsetobj,
        "scsetobj",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_scsetcode,
        "scsetcode",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_scgetobj,
        "scgetobj",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_scgethandle,
        "scgethandle",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_scgetobjidx,
        "scgetobjidx",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_scsetdesc,
        "scsetdesc",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_scobjcount,
        "scobjcount",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_setobjsc,
        "setobjsc",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_getobjsc,
        "getobjsc",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_serialize,
        "serialize",
        3,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_deserialize,
        "deserialize",
        6,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj, MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_wval,
        "wval",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int16, MVM_operand_int16 }
    },
    {
        MVM_OP_wval_wide,
        "wval_wide",
        3,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_int16, MVM_operand_int64 }
    },
    {
        MVM_OP_scwbdisable,
        "scwbdisable",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_scwbenable,
        "scwbenable",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
    {
        MVM_OP_pushcompsc,
        "pushcompsc",
        1,
        { MVM_operand_read_reg | MVM_operand_obj }
    },
    {
        MVM_OP_popcompsc,
        "popcompsc",
        1,
        { MVM_operand_write_reg | MVM_operand_obj }
    },
};

static MVMOpInfo *MVM_op_info[] = {
    MVM_op_info_primitives,
    MVM_op_info_dev,
    MVM_op_info_string,
    MVM_op_info_math,
    MVM_op_info_object,
    MVM_op_info_io,
    MVM_op_info_processthread,
    MVM_op_info_serialization,
};

static unsigned char MVM_op_banks = 8;

static unsigned char MVM_opcounts_by_bank[] = {
    178,
    2,
    53,
    57,
    135,
    51,
    32,
    19,
};

MVMOpInfo * MVM_op_get_op(unsigned char bank, unsigned char op) {
    if (bank >= MVM_op_banks || op >= MVM_opcounts_by_bank[bank])
        return NULL;
    return &MVM_op_info[bank][op];
}
