# This file is generated from src/core/oplist by tools/update_ops.p6.

class MAST::OpBanks {
    our $primitives := 0;
    our $dev := 1;
    our $string := 2;
    our $math := 3;
    our $object := 4;
    our $io := 5;
    our $processthread := 6;
    our $serialization := 7;
}
class MAST::Operands {
    our $MVM_operand_literal     := 0;
    our $MVM_operand_read_reg    := 1;
    our $MVM_operand_write_reg   := 2;
    our $MVM_operand_read_lex    := 3;
    our $MVM_operand_write_lex   := 4;
    our $MVM_operand_rw_mask     := 7;
    our $MVM_reg_int8            := 1;
    our $MVM_reg_int16           := 2;
    our $MVM_reg_int32           := 3;
    our $MVM_reg_int64           := 4;
    our $MVM_reg_num32           := 5;
    our $MVM_reg_num64           := 6;
    our $MVM_reg_str             := 7;
    our $MVM_reg_obj             := 8;
    our $MVM_operand_int8        := ($MVM_reg_int8 * 8);
    our $MVM_operand_int16       := ($MVM_reg_int16 * 8);
    our $MVM_operand_int32       := ($MVM_reg_int32 * 8);
    our $MVM_operand_int64       := ($MVM_reg_int64 * 8);
    our $MVM_operand_num32       := ($MVM_reg_num32 * 8);
    our $MVM_operand_num64       := ($MVM_reg_num64 * 8);
    our $MVM_operand_str         := ($MVM_reg_str * 8);
    our $MVM_operand_obj         := ($MVM_reg_obj * 8);
    our $MVM_operand_ins         := (9 * 8);
    our $MVM_operand_type_var    := (10 * 8);
    our $MVM_operand_lex_outer   := (11 * 8);
    our $MVM_operand_coderef     := (12 * 8);
    our $MVM_operand_callsite    := (13 * 8);
    our $MVM_operand_type_mask   := (15 * 8);
}


class MAST::Ops {
    my $MVM_operand_literal     := 0;
    my $MVM_operand_read_reg    := 1;
    my $MVM_operand_write_reg   := 2;
    my $MVM_operand_read_lex    := 3;
    my $MVM_operand_write_lex   := 4;
    my $MVM_operand_rw_mask     := 7;
    my $MVM_reg_int8            := 1;
    my $MVM_reg_int16           := 2;
    my $MVM_reg_int32           := 3;
    my $MVM_reg_int64           := 4;
    my $MVM_reg_num32           := 5;
    my $MVM_reg_num64           := 6;
    my $MVM_reg_str             := 7;
    my $MVM_reg_obj             := 8;
    my $MVM_operand_int8        := ($MVM_reg_int8 * 8);
    my $MVM_operand_int16       := ($MVM_reg_int16 * 8);
    my $MVM_operand_int32       := ($MVM_reg_int32 * 8);
    my $MVM_operand_int64       := ($MVM_reg_int64 * 8);
    my $MVM_operand_num32       := ($MVM_reg_num32 * 8);
    my $MVM_operand_num64       := ($MVM_reg_num64 * 8);
    my $MVM_operand_str         := ($MVM_reg_str * 8);
    my $MVM_operand_obj         := ($MVM_reg_obj * 8);
    my $MVM_operand_ins         := (9 * 8);
    my $MVM_operand_type_var    := (10 * 8);
    my $MVM_operand_lex_outer   := (11 * 8);
    my $MVM_operand_coderef     := (12 * 8);
    my $MVM_operand_callsite    := (13 * 8);
    my $MVM_operand_type_mask   := (15 * 8);
    our $allops := [
        [
            'no_op', nqp::hash(
                'code', 0,
                'operands', [
                ]
            ),
            'goto', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_ins
                ]
            ),
            'if_i', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_ins
                ]
            ),
            'unless_i', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_ins
                ]
            ),
            'if_n', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_ins
                ]
            ),
            'unless_n', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_ins
                ]
            ),
            'if_s', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'unless_s', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'if_s0', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'unless_s0', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'if_o', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_ins
                ]
            ),
            'unless_o', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_ins
                ]
            ),
            'set', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_type_var,
                    $MVM_operand_read_reg +| $MVM_operand_type_var
                ]
            ),
            'extend_u8', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int8
                ]
            ),
            'extend_u16', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int16
                ]
            ),
            'extend_u32', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int32
                ]
            ),
            'extend_i8', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int8
                ]
            ),
            'extend_i16', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int16
                ]
            ),
            'extend_i32', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int32
                ]
            ),
            'trunc_u8', nqp::hash(
                'code', 19,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int8,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'trunc_u16', nqp::hash(
                'code', 20,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'trunc_u32', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int32,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'trunc_i8', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int8,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'trunc_i16', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'trunc_i32', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int32,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'extend_n32', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num32
                ]
            ),
            'trunc_n32', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num32,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'getlex', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_type_var,
                    $MVM_operand_read_lex +| $MVM_operand_type_var
                ]
            ),
            'bindlex', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_write_lex +| $MVM_operand_type_var,
                    $MVM_operand_read_reg +| $MVM_operand_type_var
                ]
            ),
            'getlex_ni', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_str
                ]
            ),
            'getlex_nn', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_str
                ]
            ),
            'getlex_ns', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_str
                ]
            ),
            'getlex_no', nqp::hash(
                'code', 32,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_str
                ]
            ),
            'bindlex_ni', nqp::hash(
                'code', 33,
                'operands', [
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bindlex_nn', nqp::hash(
                'code', 34,
                'operands', [
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'bindlex_ns', nqp::hash(
                'code', 35,
                'operands', [
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindlex_no', nqp::hash(
                'code', 36,
                'operands', [
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getlex_ng', nqp::hash(
                'code', 37,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindlex_ng', nqp::hash(
                'code', 38,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'return_i', nqp::hash(
                'code', 39,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'return_n', nqp::hash(
                'code', 40,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'return_s', nqp::hash(
                'code', 41,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'return_o', nqp::hash(
                'code', 42,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'return', nqp::hash(
                'code', 43,
                'operands', [
                ]
            ),
            'const_i8', nqp::hash(
                'code', 44,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int8,
                    $MVM_operand_int8
                ]
            ),
            'const_i16', nqp::hash(
                'code', 45,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int16,
                    $MVM_operand_int16
                ]
            ),
            'const_i32', nqp::hash(
                'code', 46,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int32,
                    $MVM_operand_int32
                ]
            ),
            'const_i64', nqp::hash(
                'code', 47,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_int64
                ]
            ),
            'const_n32', nqp::hash(
                'code', 48,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num32,
                    $MVM_operand_num32
                ]
            ),
            'const_n64', nqp::hash(
                'code', 49,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_num64
                ]
            ),
            'const_s', nqp::hash(
                'code', 50,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_str
                ]
            ),
            'add_i', nqp::hash(
                'code', 51,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'sub_i', nqp::hash(
                'code', 52,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'mul_i', nqp::hash(
                'code', 53,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'div_i', nqp::hash(
                'code', 54,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'div_u', nqp::hash(
                'code', 55,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'mod_i', nqp::hash(
                'code', 56,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'mod_u', nqp::hash(
                'code', 57,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'neg_i', nqp::hash(
                'code', 58,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'abs_i', nqp::hash(
                'code', 59,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'inc_i', nqp::hash(
                'code', 60,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'inc_u', nqp::hash(
                'code', 61,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'dec_i', nqp::hash(
                'code', 62,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'dec_u', nqp::hash(
                'code', 63,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'getcode', nqp::hash(
                'code', 64,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_coderef
                ]
            ),
            'prepargs', nqp::hash(
                'code', 65,
                'operands', [
                    $MVM_operand_callsite
                ]
            ),
            'arg_i', nqp::hash(
                'code', 66,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'arg_n', nqp::hash(
                'code', 67,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'arg_s', nqp::hash(
                'code', 68,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'arg_o', nqp::hash(
                'code', 69,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'invoke_v', nqp::hash(
                'code', 70,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'invoke_i', nqp::hash(
                'code', 71,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'invoke_n', nqp::hash(
                'code', 72,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'invoke_s', nqp::hash(
                'code', 73,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'invoke_o', nqp::hash(
                'code', 74,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'add_n', nqp::hash(
                'code', 75,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'sub_n', nqp::hash(
                'code', 76,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'mul_n', nqp::hash(
                'code', 77,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'div_n', nqp::hash(
                'code', 78,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'neg_n', nqp::hash(
                'code', 79,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'abs_n', nqp::hash(
                'code', 80,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'eq_i', nqp::hash(
                'code', 81,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'ne_i', nqp::hash(
                'code', 82,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'lt_i', nqp::hash(
                'code', 83,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'le_i', nqp::hash(
                'code', 84,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'gt_i', nqp::hash(
                'code', 85,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'ge_i', nqp::hash(
                'code', 86,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'eq_n', nqp::hash(
                'code', 87,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'ne_n', nqp::hash(
                'code', 88,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'lt_n', nqp::hash(
                'code', 89,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'le_n', nqp::hash(
                'code', 90,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'gt_n', nqp::hash(
                'code', 91,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'ge_n', nqp::hash(
                'code', 92,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'argconst_i', nqp::hash(
                'code', 93,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_int64
                ]
            ),
            'argconst_n', nqp::hash(
                'code', 94,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_num64
                ]
            ),
            'argconst_s', nqp::hash(
                'code', 95,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_str
                ]
            ),
            'checkarity', nqp::hash(
                'code', 96,
                'operands', [
                    $MVM_operand_int16,
                    $MVM_operand_int16
                ]
            ),
            'param_rp_i', nqp::hash(
                'code', 97,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_int16
                ]
            ),
            'param_rp_n', nqp::hash(
                'code', 98,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_int16
                ]
            ),
            'param_rp_s', nqp::hash(
                'code', 99,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'param_rp_o', nqp::hash(
                'code', 100,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int16
                ]
            ),
            'param_op_i', nqp::hash(
                'code', 101,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_int16,
                    $MVM_operand_ins
                ]
            ),
            'param_op_n', nqp::hash(
                'code', 102,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_int16,
                    $MVM_operand_ins
                ]
            ),
            'param_op_s', nqp::hash(
                'code', 103,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_int16,
                    $MVM_operand_ins
                ]
            ),
            'param_op_o', nqp::hash(
                'code', 104,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int16,
                    $MVM_operand_ins
                ]
            ),
            'param_rn_i', nqp::hash(
                'code', 105,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_str
                ]
            ),
            'param_rn_n', nqp::hash(
                'code', 106,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_str
                ]
            ),
            'param_rn_s', nqp::hash(
                'code', 107,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_str
                ]
            ),
            'param_rn_o', nqp::hash(
                'code', 108,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_str
                ]
            ),
            'param_on_i', nqp::hash(
                'code', 109,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'param_on_n', nqp::hash(
                'code', 110,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'param_on_s', nqp::hash(
                'code', 111,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'param_on_o', nqp::hash(
                'code', 112,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'coerce_in', nqp::hash(
                'code', 113,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'coerce_ni', nqp::hash(
                'code', 114,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'band_i', nqp::hash(
                'code', 115,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bor_i', nqp::hash(
                'code', 116,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bxor_i', nqp::hash(
                'code', 117,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bnot_i', nqp::hash(
                'code', 118,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'blshift_i', nqp::hash(
                'code', 119,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'brshift_i', nqp::hash(
                'code', 120,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'pow_i', nqp::hash(
                'code', 121,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'pow_n', nqp::hash(
                'code', 122,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'takeclosure', nqp::hash(
                'code', 123,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'jumplist', nqp::hash(
                'code', 124,
                'operands', [
                    $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'caller', nqp::hash(
                'code', 125,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'getdynlex', nqp::hash(
                'code', 126,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'binddynlex', nqp::hash(
                'code', 127,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_is', nqp::hash(
                'code', 128,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'coerce_ns', nqp::hash(
                'code', 129,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'coerce_si', nqp::hash(
                'code', 130,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'coerce_sn', nqp::hash(
                'code', 131,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'smrt_numify', nqp::hash(
                'code', 132,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'smrt_strify', nqp::hash(
                'code', 133,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'param_sp', nqp::hash(
                'code', 134,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int16
                ]
            ),
            'param_sn', nqp::hash(
                'code', 135,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'ifnonnull', nqp::hash(
                'code', 136,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_ins
                ]
            ),
            'cmp_i', nqp::hash(
                'code', 137,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'cmp_n', nqp::hash(
                'code', 138,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'not_i', nqp::hash(
                'code', 139,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'setlexvalue', nqp::hash(
                'code', 140,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_int16
                ]
            ),
            'exception', nqp::hash(
                'code', 141,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'handled', nqp::hash(
                'code', 142,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'newexception', nqp::hash(
                'code', 143,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bindexmessage', nqp::hash(
                'code', 144,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindexpayload', nqp::hash(
                'code', 145,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bindexcategory', nqp::hash(
                'code', 146,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'getexmessage', nqp::hash(
                'code', 147,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getexpayload', nqp::hash(
                'code', 148,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getexcategory', nqp::hash(
                'code', 149,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'throwdyn', nqp::hash(
                'code', 150,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'throwlex', nqp::hash(
                'code', 151,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'throwlexotic', nqp::hash(
                'code', 152,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'throwcatdyn', nqp::hash(
                'code', 153,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int64
                ]
            ),
            'throwcatlex', nqp::hash(
                'code', 154,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int64
                ]
            ),
            'throwcatlexotic', nqp::hash(
                'code', 155,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int64
                ]
            ),
            'die', nqp::hash(
                'code', 156,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'newlexotic', nqp::hash(
                'code', 157,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_ins
                ]
            ),
            'lexoticresult', nqp::hash(
                'code', 158,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'mod_n', nqp::hash(
                'code', 159,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'usecapture', nqp::hash(
                'code', 160,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'savecapture', nqp::hash(
                'code', 161,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'captureposelems', nqp::hash(
                'code', 162,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'captureposarg', nqp::hash(
                'code', 163,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'captureposarg_i', nqp::hash(
                'code', 164,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'captureposarg_n', nqp::hash(
                'code', 165,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'captureposarg_s', nqp::hash(
                'code', 166,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'captureposprimspec', nqp::hash(
                'code', 167,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'invokewithcapture', nqp::hash(
                'code', 168,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'multicacheadd', nqp::hash(
                'code', 169,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'multicachefind', nqp::hash(
                'code', 170,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'lexprimspec', nqp::hash(
                'code', 171,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'ceil_n', nqp::hash(
                'code', 172,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'floor_n', nqp::hash(
                'code', 173,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'assign', nqp::hash(
                'code', 174,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'assignunchecked', nqp::hash(
                'code', 175,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'objprimspec', nqp::hash(
                'code', 176,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'backtracestrings', nqp::hash(
                'code', 177,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            )
        ],
        [
            'sleep', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'say_I', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            )
        ],
        [
            'concat_s', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'repeat_s', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'substr_s', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'index_s', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'graphs_s', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'codes_s', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'eq_s', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'ne_s', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'eqat_s', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'haveat_s', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'getcp_s', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'indexcp_s', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'uc', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'lc', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'tc', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'buftostr', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'strtobuf', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'decode_s', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'decode_b', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'decode', nqp::hash(
                'code', 19,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'encode', nqp::hash(
                'code', 20,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'split', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'join', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'replace', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getcpbyname', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'indexat_scb', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_str,
                    $MVM_operand_ins
                ]
            ),
            'unipropcode', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'unipvalcode', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'hasuniprop', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'hasunipropc', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_int16,
                    $MVM_operand_int16
                ]
            ),
            'concatr_s', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'splice_s', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'chars', nqp::hash(
                'code', 32,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'chr', nqp::hash(
                'code', 33,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'ordfirst', nqp::hash(
                'code', 34,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'ordat', nqp::hash(
                'code', 35,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'rindexfrom', nqp::hash(
                'code', 36,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'escape', nqp::hash(
                'code', 37,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'flip', nqp::hash(
                'code', 38,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'iscclass', nqp::hash(
                'code', 39,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'findcclass', nqp::hash(
                'code', 40,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'findnotcclass', nqp::hash(
                'code', 41,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'nfafromstatelist', nqp::hash(
                'code', 42,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'nfarunproto', nqp::hash(
                'code', 43,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'nfarunalt', nqp::hash(
                'code', 44,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'flattenropes', nqp::hash(
                'code', 45,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'gt_s', nqp::hash(
                'code', 46,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'ge_s', nqp::hash(
                'code', 47,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'lt_s', nqp::hash(
                'code', 48,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'le_s', nqp::hash(
                'code', 49,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'cmp_s', nqp::hash(
                'code', 50,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'radix', nqp::hash(
                'code', 51,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'eqatic_s', nqp::hash(
                'code', 52,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            )
        ],
        [
            'sin_n', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'asin_n', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'cos_n', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'acos_n', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'tan_n', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'atan_n', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'atan2_n', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_write_reg +| $MVM_operand_num64
                ]
            ),
            'sec_n', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'asec_n', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'sinh_n', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'cosh_n', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'tanh_n', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'sech_n', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'sqrt_n', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'gcd_i', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'lcm_i', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'add_I', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'sub_I', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'mul_I', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'div_I', nqp::hash(
                'code', 19,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'mod_I', nqp::hash(
                'code', 20,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'neg_I', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'abs_I', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'inc_I', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'dec_I', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'cmp_I', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'eq_I', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ne_I', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'lt_I', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'le_I', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'gt_I', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ge_I', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'not_I', nqp::hash(
                'code', 32,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bor_I', nqp::hash(
                'code', 33,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bxor_I', nqp::hash(
                'code', 34,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'band_I', nqp::hash(
                'code', 35,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bnot_I', nqp::hash(
                'code', 36,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'blshift_I', nqp::hash(
                'code', 37,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'brshift_I', nqp::hash(
                'code', 38,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pow_I', nqp::hash(
                'code', 39,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'gcd_I', nqp::hash(
                'code', 40,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'lcm_I', nqp::hash(
                'code', 41,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'expmod_I', nqp::hash(
                'code', 42,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isprime_I', nqp::hash(
                'code', 43,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'rand_I', nqp::hash(
                'code', 44,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_Ii', nqp::hash(
                'code', 45,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_In', nqp::hash(
                'code', 46,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_Is', nqp::hash(
                'code', 47,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_iI', nqp::hash(
                'code', 48,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_nI', nqp::hash(
                'code', 49,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'coerce_sI', nqp::hash(
                'code', 50,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isbig_I', nqp::hash(
                'code', 51,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'base_I', nqp::hash(
                'code', 52,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'radix_I', nqp::hash(
                'code', 53,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'div_In', nqp::hash(
                'code', 54,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'log_n', nqp::hash(
                'code', 55,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'exp_n', nqp::hash(
                'code', 56,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            )
        ],
        [
            'knowhow', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'findmeth', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str
                ]
            ),
            'findmeth_s', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'can', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str
                ]
            ),
            'can_s', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'create', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'gethow', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getwhat', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'atkey_i', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'atkey_n', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'atkey_s', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'atkey_o', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindkey_i', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bindkey_n', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'bindkey_s', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindkey_o', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'existskey', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'deletekey', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getwhere', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'eqaddr', nqp::hash(
                'code', 19,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'reprname', nqp::hash(
                'code', 20,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isconcrete', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'atpos_i', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'atpos_n', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'atpos_s', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'atpos_o', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bindpos_i', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bindpos_n', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'bindpos_s', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindpos_o', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'push_i', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'push_n', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'push_s', nqp::hash(
                'code', 32,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'push_o', nqp::hash(
                'code', 33,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pop_i', nqp::hash(
                'code', 34,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pop_n', nqp::hash(
                'code', 35,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pop_s', nqp::hash(
                'code', 36,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pop_o', nqp::hash(
                'code', 37,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'shift_i', nqp::hash(
                'code', 38,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'shift_n', nqp::hash(
                'code', 39,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'shift_s', nqp::hash(
                'code', 40,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'shift_o', nqp::hash(
                'code', 41,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'unshift_i', nqp::hash(
                'code', 42,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'unshift_n', nqp::hash(
                'code', 43,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'unshift_s', nqp::hash(
                'code', 44,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'unshift_o', nqp::hash(
                'code', 45,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'splice', nqp::hash(
                'code', 46,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            '__INVALID_1__', nqp::hash(
                'code', 47,
                'operands', [
                ]
            ),
            'setelemspos', nqp::hash(
                'code', 48,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'box_i', nqp::hash(
                'code', 49,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'box_n', nqp::hash(
                'code', 50,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'box_s', nqp::hash(
                'code', 51,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'unbox_i', nqp::hash(
                'code', 52,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'unbox_n', nqp::hash(
                'code', 53,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'unbox_s', nqp::hash(
                'code', 54,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bindattr_i', nqp::hash(
                'code', 55,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_int16
                ]
            ),
            'bindattr_n', nqp::hash(
                'code', 56,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_num64,
                    $MVM_operand_int16
                ]
            ),
            'bindattr_s', nqp::hash(
                'code', 57,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'bindattr_o', nqp::hash(
                'code', 58,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_int16
                ]
            ),
            'bindattrs_i', nqp::hash(
                'code', 59,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'bindattrs_n', nqp::hash(
                'code', 60,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_num64
                ]
            ),
            'bindattrs_s', nqp::hash(
                'code', 61,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindattrs_o', nqp::hash(
                'code', 62,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getattr_i', nqp::hash(
                'code', 63,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'getattr_n', nqp::hash(
                'code', 64,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'getattr_s', nqp::hash(
                'code', 65,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'getattr_o', nqp::hash(
                'code', 66,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_str,
                    $MVM_operand_int16
                ]
            ),
            'getattrs_i', nqp::hash(
                'code', 67,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getattrs_n', nqp::hash(
                'code', 68,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getattrs_s', nqp::hash(
                'code', 69,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getattrs_o', nqp::hash(
                'code', 70,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'isnull', nqp::hash(
                'code', 71,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'knowhowattr', nqp::hash(
                'code', 72,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'iscoderef', nqp::hash(
                'code', 73,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'null', nqp::hash(
                'code', 74,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'clone', nqp::hash(
                'code', 75,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isnull_s', nqp::hash(
                'code', 76,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bootint', nqp::hash(
                'code', 77,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootnum', nqp::hash(
                'code', 78,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootstr', nqp::hash(
                'code', 79,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootarray', nqp::hash(
                'code', 80,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'boothash', nqp::hash(
                'code', 81,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'sethllconfig', nqp::hash(
                'code', 82,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'hllboxtype_i', nqp::hash(
                'code', 83,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'hllboxtype_n', nqp::hash(
                'code', 84,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'hllboxtype_s', nqp::hash(
                'code', 85,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'elems', nqp::hash(
                'code', 86,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'null_s', nqp::hash(
                'code', 87,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str
                ]
            ),
            'newtype', nqp::hash(
                'code', 88,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'islist', nqp::hash(
                'code', 89,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ishash', nqp::hash(
                'code', 90,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'iter', nqp::hash(
                'code', 91,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'iterkey_s', nqp::hash(
                'code', 92,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'iterval', nqp::hash(
                'code', 93,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getcodename', nqp::hash(
                'code', 94,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'composetype', nqp::hash(
                'code', 95,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setmethcache', nqp::hash(
                'code', 96,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setmethcacheauth', nqp::hash(
                'code', 97,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'settypecache', nqp::hash(
                'code', 98,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setinvokespec', nqp::hash(
                'code', 99,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isinvokable', nqp::hash(
                'code', 100,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'iscont', nqp::hash(
                'code', 101,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'decont', nqp::hash(
                'code', 102,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setboolspec', nqp::hash(
                'code', 103,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'istrue', nqp::hash(
                'code', 104,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'isfalse', nqp::hash(
                'code', 105,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'istrue_s', nqp::hash(
                'code', 106,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'isfalse_s', nqp::hash(
                'code', 107,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getcodeobj', nqp::hash(
                'code', 108,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setcodeobj', nqp::hash(
                'code', 109,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setcodename', nqp::hash(
                'code', 110,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'forceouterctx', nqp::hash(
                'code', 111,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getcomp', nqp::hash(
                'code', 112,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindcomp', nqp::hash(
                'code', 113,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getcurhllsym', nqp::hash(
                'code', 114,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'bindcurhllsym', nqp::hash(
                'code', 115,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getwho', nqp::hash(
                'code', 116,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setwho', nqp::hash(
                'code', 117,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'rebless', nqp::hash(
                'code', 118,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'istype', nqp::hash(
                'code', 119,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ctx', nqp::hash(
                'code', 120,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'ctxouter', nqp::hash(
                'code', 121,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ctxcaller', nqp::hash(
                'code', 122,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'ctxlexpad', nqp::hash(
                'code', 123,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'curcode', nqp::hash(
                'code', 124,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'callercode', nqp::hash(
                'code', 125,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootintarray', nqp::hash(
                'code', 126,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootnumarray', nqp::hash(
                'code', 127,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'bootstrarray', nqp::hash(
                'code', 128,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'hlllist', nqp::hash(
                'code', 129,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'hllhash', nqp::hash(
                'code', 130,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'attrinited', nqp::hash(
                'code', 131,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'setcontspec', nqp::hash(
                'code', 132,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'existspos', nqp::hash(
                'code', 133,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'gethllsym', nqp::hash(
                'code', 134,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            )
        ],
        [
            'copy_f', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'append_f', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'rename_f', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'delete_f', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'chmod_f', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'exists_f', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'mkdir', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'rmdir', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'open_dir', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'read_dir', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'close_dir', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'open_fh', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'close_fh', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'read_fhs', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'read_fhbuf', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'slurp', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'spew', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'write_fhs', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'write_fhbuf', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'seek_fh', nqp::hash(
                'code', 19,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'lock_fh', nqp::hash(
                'code', 20,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'unlock_fh', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'flush_fh', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'sync_fh', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'pipe_fh', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'trunc_fh', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'eof_fh', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getstdin', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'getstdout', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'getstderr', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'connect_sk', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'close_sk', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'bind_sk', nqp::hash(
                'code', 32,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'listen_sk', nqp::hash(
                'code', 33,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'accept_sk', nqp::hash(
                'code', 34,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'send_sks', nqp::hash(
                'code', 35,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'send_skbuf', nqp::hash(
                'code', 36,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'recv_sks', nqp::hash(
                'code', 37,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'recv_skbuf', nqp::hash(
                'code', 38,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'getaddr_sk', nqp::hash(
                'code', 39,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'hostname', nqp::hash(
                'code', 40,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str
                ]
            ),
            'nametoaddr', nqp::hash(
                'code', 41,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'addrtoname', nqp::hash(
                'code', 42,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'porttosvc', nqp::hash(
                'code', 43,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'setencoding', nqp::hash(
                'code', 44,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'print', nqp::hash(
                'code', 45,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'say', nqp::hash(
                'code', 46,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'readall_fh', nqp::hash(
                'code', 47,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'tell_fh', nqp::hash(
                'code', 48,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'stat', nqp::hash(
                'code', 49,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'readline_fh', nqp::hash(
                'code', 50,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            )
        ],
        [
            'getenv', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'setenv', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'delenv', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'nametogid', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'gidtoname', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'nametouid', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'uidtoname', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'getusername', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str
                ]
            ),
            'getuid', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'getgid', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'gethomedir', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str
                ]
            ),
            'getencoding', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str
                ]
            ),
            'procshell', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'procshellbg', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'procrun', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'procrunbg', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'prockill', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'procwait', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'procalive', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'detach', nqp::hash(
                'code', 19,
                'operands', [
                ]
            ),
            'daemonize', nqp::hash(
                'code', 20,
                'operands', [
                ]
            ),
            'chdir', nqp::hash(
                'code', 21,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'rand_i', nqp::hash(
                'code', 22,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'rand_n', nqp::hash(
                'code', 23,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64
                ]
            ),
            'time_i', nqp::hash(
                'code', 24,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64
                ]
            ),
            'clargs', nqp::hash(
                'code', 25,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'newthread', nqp::hash(
                'code', 26,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'jointhread', nqp::hash(
                'code', 27,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'time_n', nqp::hash(
                'code', 28,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_num64
                ]
            ),
            'exit', nqp::hash(
                'code', 29,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'loadbytecode', nqp::hash(
                'code', 30,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'getenvhash', nqp::hash(
                'code', 31,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            )
        ],
        [
            'sha1', nqp::hash(
                'code', 0,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'createsc', nqp::hash(
                'code', 1,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'scsetobj', nqp::hash(
                'code', 2,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'scsetcode', nqp::hash(
                'code', 3,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'scgetobj', nqp::hash(
                'code', 4,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_int64
                ]
            ),
            'scgethandle', nqp::hash(
                'code', 5,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'scgetobjidx', nqp::hash(
                'code', 6,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'scsetdesc', nqp::hash(
                'code', 7,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_str
                ]
            ),
            'scobjcount', nqp::hash(
                'code', 8,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_int64,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'setobjsc', nqp::hash(
                'code', 9,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'getobjsc', nqp::hash(
                'code', 10,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'serialize', nqp::hash(
                'code', 11,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'deserialize', nqp::hash(
                'code', 12,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_str,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj,
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'wval', nqp::hash(
                'code', 13,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int16,
                    $MVM_operand_int16
                ]
            ),
            'wval_wide', nqp::hash(
                'code', 14,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj,
                    $MVM_operand_int16,
                    $MVM_operand_int64
                ]
            ),
            'scwbdisable', nqp::hash(
                'code', 15,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'scwbenable', nqp::hash(
                'code', 16,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            ),
            'pushcompsc', nqp::hash(
                'code', 17,
                'operands', [
                    $MVM_operand_read_reg +| $MVM_operand_obj
                ]
            ),
            'popcompsc', nqp::hash(
                'code', 18,
                'operands', [
                    $MVM_operand_write_reg +| $MVM_operand_obj
                ]
            )
        ]
    ];
    our $primitives := nqp::hash();
    for $allops[0] -> $opname, $opdetails {
        $primitives{$opname} := $opdetails;
    }
    our $dev := nqp::hash();
    for $allops[1] -> $opname, $opdetails {
        $dev{$opname} := $opdetails;
    }
    our $string := nqp::hash();
    for $allops[2] -> $opname, $opdetails {
        $string{$opname} := $opdetails;
    }
    our $math := nqp::hash();
    for $allops[3] -> $opname, $opdetails {
        $math{$opname} := $opdetails;
    }
    our $object := nqp::hash();
    for $allops[4] -> $opname, $opdetails {
        $object{$opname} := $opdetails;
    }
    our $io := nqp::hash();
    for $allops[5] -> $opname, $opdetails {
        $io{$opname} := $opdetails;
    }
    our $processthread := nqp::hash();
    for $allops[6] -> $opname, $opdetails {
        $processthread{$opname} := $opdetails;
    }
    our $serialization := nqp::hash();
    for $allops[7] -> $opname, $opdetails {
        $serialization{$opname} := $opdetails;
    }
}
