# This file is generated from src/core/oplist by tools/update_ops.p6.

class MAST::OpBanks {
    our $primitives := 0;
    our $dev := 1;
    our $string := 2;
    our $math := 3;
    our $object := 4;
    our $io := 5;
    our $processthread := 6;
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
    our $primitives := nqp::hash(
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
        'get_lex', nqp::hash(
            'code', 27,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_type_var,
                $MVM_operand_read_lex +| $MVM_operand_type_var
            ]
        ),
        'bind_lex', nqp::hash(
            'code', 28,
            'operands', [
                $MVM_operand_write_lex +| $MVM_operand_type_var,
                $MVM_operand_read_reg +| $MVM_operand_type_var
            ]
        ),
        'get_lex_lo', nqp::hash(
            'code', 29,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_type_var,
                $MVM_operand_lex_outer,
                $MVM_operand_read_lex +| $MVM_operand_type_var
            ]
        ),
        'bind_lex_lo', nqp::hash(
            'code', 30,
            'operands', [
                $MVM_operand_write_lex +| $MVM_operand_type_var,
                $MVM_operand_lex_outer,
                $MVM_operand_read_reg +| $MVM_operand_type_var
            ]
        ),
        'get_lex_ni', nqp::hash(
            'code', 31,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_int64,
                $MVM_operand_str
            ]
        ),
        'get_lex_nn', nqp::hash(
            'code', 32,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_num64,
                $MVM_operand_str
            ]
        ),
        'get_lex_ns', nqp::hash(
            'code', 33,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_str,
                $MVM_operand_str
            ]
        ),
        'get_lex_no', nqp::hash(
            'code', 34,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj,
                $MVM_operand_str
            ]
        ),
        'bind_lex_ni', nqp::hash(
            'code', 35,
            'operands', [
                $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'bind_lex_nn', nqp::hash(
            'code', 36,
            'operands', [
                $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_num64
            ]
        ),
        'bind_lex_ns', nqp::hash(
            'code', 37,
            'operands', [
                $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'bind_lex_no', nqp::hash(
            'code', 38,
            'operands', [
                $MVM_operand_str,
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
        )
    );
    our $dev := nqp::hash(
        'say_i', nqp::hash(
            'code', 0,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'say_s', nqp::hash(
            'code', 1,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'say_n', nqp::hash(
            'code', 2,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_num64
            ]
        ),
        'sleep', nqp::hash(
            'code', 3,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'anonoshtype', nqp::hash(
            'code', 4,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj
            ]
        )
    );
    our $string := nqp::hash(
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
                $MVM_operand_read_reg +| $MVM_operand_str
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
        'setcp_s', nqp::hash(
            'code', 11,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_int64,
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'indexcp_s', nqp::hash(
            'code', 12,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_int64,
                $MVM_operand_read_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        )
    );
    our $math := nqp::hash(
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
        )
    );
    our $object := nqp::hash(
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
        'elemskeyed', nqp::hash(
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
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'shift_n', nqp::hash(
            'code', 39,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_num64
            ]
        ),
        'shift_s', nqp::hash(
            'code', 40,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'shift_o', nqp::hash(
            'code', 41,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'unshift_i', nqp::hash(
            'code', 42,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_int64,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'unshift_n', nqp::hash(
            'code', 43,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_num64,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'unshift_s', nqp::hash(
            'code', 44,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'unshift_o', nqp::hash(
            'code', 45,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj,
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
        'elemspos', nqp::hash(
            'code', 47,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_int64,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        )
    );
    our $io := nqp::hash(
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
                $MVM_operand_read_reg +| $MVM_operand_str
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
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str
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
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_int64
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
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'spew', nqp::hash(
            'code', 16,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'write_fhs', nqp::hash(
            'code', 17,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str,
                $MVM_operand_read_reg +| $MVM_operand_int64,
                $MVM_operand_read_reg +| $MVM_operand_int64
            ]
        ),
        'write_fhbuf', nqp::hash(
            'code', 18,
            'operands', [
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
                $MVM_operand_write_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'getstdout', nqp::hash(
            'code', 28,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'getstderr', nqp::hash(
            'code', 29,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj
            ]
        ),
        'connect_sk', nqp::hash(
            'code', 30,
            'operands', [
                $MVM_operand_write_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str,
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
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str,
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
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_str
            ]
        ),
        'send_skbuf', nqp::hash(
            'code', 36,
            'operands', [
                $MVM_operand_read_reg +| $MVM_operand_obj,
                $MVM_operand_read_reg +| $MVM_operand_obj
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
        )
    );
    our $processthread := nqp::hash(
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
        )
    );
}
