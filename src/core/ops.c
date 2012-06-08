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
/* This file is generated from src/core/oplist by tools/update_ops_h.p6. */
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
        MVM_OP_get_lex,
        "get_lex",
        2,
        { MVM_operand_write_reg | MVM_operand_type_var, MVM_operand_read_lex | MVM_operand_type_var }
    },
    {
        MVM_OP_bind_lex,
        "bind_lex",
        2,
        { MVM_operand_write_lex | MVM_operand_type_var, MVM_operand_read_reg | MVM_operand_type_var }
    },
    {
        MVM_OP_get_lex_lo,
        "get_lex_lo",
        3,
        { MVM_operand_write_reg | MVM_operand_type_var, MVM_operand_lex_outer, MVM_operand_read_lex | MVM_operand_type_var }
    },
    {
        MVM_OP_bind_lex_lo,
        "bind_lex_lo",
        3,
        { MVM_operand_write_lex | MVM_operand_type_var, MVM_operand_lex_outer, MVM_operand_read_reg | MVM_operand_type_var }
    },
    {
        MVM_OP_get_lex_ni,
        "get_lex_ni",
        2,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_str }
    },
    {
        MVM_OP_get_lex_nn,
        "get_lex_nn",
        2,
        { MVM_operand_write_reg | MVM_operand_num64, MVM_operand_str }
    },
    {
        MVM_OP_get_lex_ns,
        "get_lex_ns",
        2,
        { MVM_operand_write_reg | MVM_operand_str, MVM_operand_str }
    },
    {
        MVM_OP_get_lex_no,
        "get_lex_no",
        2,
        { MVM_operand_write_reg | MVM_operand_obj, MVM_operand_str }
    },
    {
        MVM_OP_bind_lex_ni,
        "bind_lex_ni",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_bind_lex_nn,
        "bind_lex_nn",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_num64 }
    },
    {
        MVM_OP_bind_lex_ns,
        "bind_lex_ns",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_bind_lex_no,
        "bind_lex_no",
        2,
        { MVM_operand_str, MVM_operand_read_reg | MVM_operand_obj }
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
};
static MVMOpInfo MVM_op_info_dev[] = {
    {
        MVM_OP_say_i,
        "say_i",
        1,
        { MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_say_s,
        "say_s",
        1,
        { MVM_operand_read_reg | MVM_operand_str }
    },
    {
        MVM_OP_say_n,
        "say_n",
        1,
        { MVM_operand_read_reg | MVM_operand_num64 }
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
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_str }
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
        MVM_OP_setcp_s,
        "setcp_s",
        3,
        { MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_int64 }
    },
    {
        MVM_OP_indexcp_s,
        "indexcp_s",
        3,
        { MVM_operand_write_reg | MVM_operand_int64, MVM_operand_read_reg | MVM_operand_str, MVM_operand_read_reg | MVM_operand_int64 }
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
};

static MVMOpInfo *MVM_op_info[] = {
    MVM_op_info_primitives,
    MVM_op_info_dev,
    MVM_op_info_string,
    MVM_op_info_math,
};

static unsigned char MVM_op_banks = 4;

static unsigned char MVM_opcounts_by_bank[] = {
    93,
    3,
    13,
    13,
};

MVMOpInfo * MVM_op_get_op(unsigned char bank, unsigned char op) {
    if (bank >= MVM_op_banks || op >= MVM_opcounts_by_bank[bank])
        return NULL;
    return &MVM_op_info[bank][op];
}
