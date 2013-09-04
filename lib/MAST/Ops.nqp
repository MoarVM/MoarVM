# This file is generated from src/core/oplist by tools/update_ops.p6.

class MAST::OpCode {
    has $!name;
    has $!code;
    has @!operands;
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
    our $ops_list := nqp::list();
    our $operands_list := nqp::list();
    our $no_op := 0;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 0,
        name => 'no_op')
    );
    nqp::push($operands_list, nqp::list_i());
    our $goto := 1;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 1,
        name => 'goto')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_ins
    ));
    our $if_i := 2;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 2,
        name => 'if_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_ins
    ));
    our $unless_i := 3;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 3,
        name => 'unless_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_ins
    ));
    our $if_n := 4;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 4,
        name => 'if_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_ins
    ));
    our $unless_n := 5;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 5,
        name => 'unless_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_ins
    ));
    our $if_s := 6;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 6,
        name => 'if_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $unless_s := 7;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 7,
        name => 'unless_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $if_s0 := 8;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 8,
        name => 'if_s0')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $unless_s0 := 9;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 9,
        name => 'unless_s0')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $if_o := 10;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 10,
        name => 'if_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_ins
    ));
    our $unless_o := 11;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 11,
        name => 'unless_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_ins
    ));
    our $set := 12;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 12,
        name => 'set')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_type_var,
        $MVM_operand_read_reg +| $MVM_operand_type_var
    ));
    our $extend_u8 := 13;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 13,
        name => 'extend_u8')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int8
    ));
    our $extend_u16 := 14;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 14,
        name => 'extend_u16')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int16
    ));
    our $extend_u32 := 15;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 15,
        name => 'extend_u32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int32
    ));
    our $extend_i8 := 16;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 16,
        name => 'extend_i8')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int8
    ));
    our $extend_i16 := 17;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 17,
        name => 'extend_i16')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int16
    ));
    our $extend_i32 := 18;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 18,
        name => 'extend_i32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int32
    ));
    our $trunc_u8 := 19;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 19,
        name => 'trunc_u8')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int8,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $trunc_u16 := 20;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 20,
        name => 'trunc_u16')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $trunc_u32 := 21;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 21,
        name => 'trunc_u32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int32,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $trunc_i8 := 22;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 22,
        name => 'trunc_i8')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int8,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $trunc_i16 := 23;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 23,
        name => 'trunc_i16')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $trunc_i32 := 24;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 24,
        name => 'trunc_i32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int32,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $extend_n32 := 25;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 25,
        name => 'extend_n32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num32
    ));
    our $trunc_n32 := 26;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 26,
        name => 'trunc_n32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num32,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $getlex := 27;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 27,
        name => 'getlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_type_var,
        $MVM_operand_read_lex +| $MVM_operand_type_var
    ));
    our $bindlex := 28;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 28,
        name => 'bindlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_lex +| $MVM_operand_type_var,
        $MVM_operand_read_reg +| $MVM_operand_type_var
    ));
    our $getlex_ni := 29;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 29,
        name => 'getlex_ni')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_str
    ));
    our $getlex_nn := 30;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 30,
        name => 'getlex_nn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_str
    ));
    our $getlex_ns := 31;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 31,
        name => 'getlex_ns')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_str
    ));
    our $getlex_no := 32;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 32,
        name => 'getlex_no')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_str
    ));
    our $bindlex_ni := 33;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 33,
        name => 'bindlex_ni')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bindlex_nn := 34;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 34,
        name => 'bindlex_nn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $bindlex_ns := 35;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 35,
        name => 'bindlex_ns')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindlex_no := 36;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 36,
        name => 'bindlex_no')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getlex_ng := 37;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 37,
        name => 'getlex_ng')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindlex_ng := 38;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 38,
        name => 'bindlex_ng')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $return_i := 39;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 39,
        name => 'return_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $return_n := 40;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 40,
        name => 'return_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $return_s := 41;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 41,
        name => 'return_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $return_o := 42;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 42,
        name => 'return_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $return := 43;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 43,
        name => 'return')
    );
    nqp::push($operands_list, nqp::list_i());
    our $const_i8 := 44;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 44,
        name => 'const_i8')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int8,
        $MVM_operand_int8
    ));
    our $const_i16 := 45;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 45,
        name => 'const_i16')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int16,
        $MVM_operand_int16
    ));
    our $const_i32 := 46;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 46,
        name => 'const_i32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int32,
        $MVM_operand_int32
    ));
    our $const_i64 := 47;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 47,
        name => 'const_i64')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_int64
    ));
    our $const_n32 := 48;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 48,
        name => 'const_n32')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num32,
        $MVM_operand_num32
    ));
    our $const_n64 := 49;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 49,
        name => 'const_n64')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_num64
    ));
    our $const_s := 50;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 50,
        name => 'const_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_str
    ));
    our $add_i := 51;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 51,
        name => 'add_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $sub_i := 52;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 52,
        name => 'sub_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $mul_i := 53;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 53,
        name => 'mul_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $div_i := 54;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 54,
        name => 'div_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $div_u := 55;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 55,
        name => 'div_u')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $mod_i := 56;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 56,
        name => 'mod_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $mod_u := 57;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 57,
        name => 'mod_u')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $neg_i := 58;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 58,
        name => 'neg_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $abs_i := 59;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 59,
        name => 'abs_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $inc_i := 60;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 60,
        name => 'inc_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $inc_u := 61;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 61,
        name => 'inc_u')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $dec_i := 62;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 62,
        name => 'dec_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $dec_u := 63;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 63,
        name => 'dec_u')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $getcode := 64;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 64,
        name => 'getcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_coderef
    ));
    our $prepargs := 65;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 65,
        name => 'prepargs')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_callsite
    ));
    our $arg_i := 66;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 66,
        name => 'arg_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $arg_n := 67;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 67,
        name => 'arg_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $arg_s := 68;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 68,
        name => 'arg_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $arg_o := 69;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 69,
        name => 'arg_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $invoke_v := 70;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 70,
        name => 'invoke_v')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $invoke_i := 71;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 71,
        name => 'invoke_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $invoke_n := 72;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 72,
        name => 'invoke_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $invoke_s := 73;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 73,
        name => 'invoke_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $invoke_o := 74;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 74,
        name => 'invoke_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $add_n := 75;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 75,
        name => 'add_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $sub_n := 76;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 76,
        name => 'sub_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $mul_n := 77;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 77,
        name => 'mul_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $div_n := 78;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 78,
        name => 'div_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $neg_n := 79;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 79,
        name => 'neg_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $abs_n := 80;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 80,
        name => 'abs_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $eq_i := 81;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 81,
        name => 'eq_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $ne_i := 82;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 82,
        name => 'ne_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $lt_i := 83;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 83,
        name => 'lt_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $le_i := 84;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 84,
        name => 'le_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $gt_i := 85;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 85,
        name => 'gt_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $ge_i := 86;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 86,
        name => 'ge_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $eq_n := 87;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 87,
        name => 'eq_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $ne_n := 88;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 88,
        name => 'ne_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $lt_n := 89;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 89,
        name => 'lt_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $le_n := 90;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 90,
        name => 'le_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $gt_n := 91;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 91,
        name => 'gt_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $ge_n := 92;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 92,
        name => 'ge_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $argconst_i := 93;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 93,
        name => 'argconst_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_int64
    ));
    our $argconst_n := 94;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 94,
        name => 'argconst_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_num64
    ));
    our $argconst_s := 95;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 95,
        name => 'argconst_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_str
    ));
    our $checkarity := 96;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 96,
        name => 'checkarity')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int16,
        $MVM_operand_int16
    ));
    our $param_rp_i := 97;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 97,
        name => 'param_rp_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_int16
    ));
    our $param_rp_n := 98;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 98,
        name => 'param_rp_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_int16
    ));
    our $param_rp_s := 99;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 99,
        name => 'param_rp_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $param_rp_o := 100;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 100,
        name => 'param_rp_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int16
    ));
    our $param_op_i := 101;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 101,
        name => 'param_op_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_int16,
        $MVM_operand_ins
    ));
    our $param_op_n := 102;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 102,
        name => 'param_op_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_int16,
        $MVM_operand_ins
    ));
    our $param_op_s := 103;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 103,
        name => 'param_op_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_int16,
        $MVM_operand_ins
    ));
    our $param_op_o := 104;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 104,
        name => 'param_op_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int16,
        $MVM_operand_ins
    ));
    our $param_rn_i := 105;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 105,
        name => 'param_rn_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_str
    ));
    our $param_rn_n := 106;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 106,
        name => 'param_rn_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_str
    ));
    our $param_rn_s := 107;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 107,
        name => 'param_rn_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_str
    ));
    our $param_rn_o := 108;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 108,
        name => 'param_rn_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_str
    ));
    our $param_on_i := 109;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 109,
        name => 'param_on_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $param_on_n := 110;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 110,
        name => 'param_on_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $param_on_s := 111;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 111,
        name => 'param_on_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $param_on_o := 112;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 112,
        name => 'param_on_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $coerce_in := 113;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 113,
        name => 'coerce_in')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $coerce_ni := 114;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 114,
        name => 'coerce_ni')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $band_i := 115;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 115,
        name => 'band_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bor_i := 116;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 116,
        name => 'bor_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bxor_i := 117;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 117,
        name => 'bxor_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bnot_i := 118;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 118,
        name => 'bnot_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $blshift_i := 119;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 119,
        name => 'blshift_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $brshift_i := 120;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 120,
        name => 'brshift_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $pow_i := 121;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 121,
        name => 'pow_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $pow_n := 122;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 122,
        name => 'pow_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $takeclosure := 123;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 123,
        name => 'takeclosure')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $jumplist := 124;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 124,
        name => 'jumplist')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $caller := 125;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 125,
        name => 'caller')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $getdynlex := 126;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 126,
        name => 'getdynlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $binddynlex := 127;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 127,
        name => 'binddynlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_is := 128;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 128,
        name => 'coerce_is')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $coerce_ns := 129;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 129,
        name => 'coerce_ns')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $coerce_si := 130;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 130,
        name => 'coerce_si')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $coerce_sn := 131;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 131,
        name => 'coerce_sn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $smrt_numify := 132;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 132,
        name => 'smrt_numify')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $smrt_strify := 133;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 133,
        name => 'smrt_strify')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $param_sp := 134;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 134,
        name => 'param_sp')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int16
    ));
    our $param_sn := 135;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 135,
        name => 'param_sn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $ifnonnull := 136;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 136,
        name => 'ifnonnull')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_ins
    ));
    our $cmp_i := 137;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 137,
        name => 'cmp_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $cmp_n := 138;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 138,
        name => 'cmp_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $not_i := 139;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 139,
        name => 'not_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $setlexvalue := 140;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 140,
        name => 'setlexvalue')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_int16
    ));
    our $exception := 141;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 141,
        name => 'exception')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $handled := 142;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 142,
        name => 'handled')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $newexception := 143;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 143,
        name => 'newexception')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bindexmessage := 144;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 144,
        name => 'bindexmessage')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindexpayload := 145;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 145,
        name => 'bindexpayload')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bindexcategory := 146;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 146,
        name => 'bindexcategory')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $getexmessage := 147;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 147,
        name => 'getexmessage')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getexpayload := 148;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 148,
        name => 'getexpayload')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getexcategory := 149;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 149,
        name => 'getexcategory')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $throwdyn := 150;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 150,
        name => 'throwdyn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $throwlex := 151;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 151,
        name => 'throwlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $throwlexotic := 152;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 152,
        name => 'throwlexotic')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $throwcatdyn := 153;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 153,
        name => 'throwcatdyn')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int64
    ));
    our $throwcatlex := 154;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 154,
        name => 'throwcatlex')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int64
    ));
    our $throwcatlexotic := 155;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 155,
        name => 'throwcatlexotic')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int64
    ));
    our $die := 156;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 156,
        name => 'die')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $newlexotic := 157;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 157,
        name => 'newlexotic')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_ins
    ));
    our $lexoticresult := 158;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 158,
        name => 'lexoticresult')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $mod_n := 159;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 159,
        name => 'mod_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $usecapture := 160;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 160,
        name => 'usecapture')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $savecapture := 161;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 161,
        name => 'savecapture')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $captureposelems := 162;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 162,
        name => 'captureposelems')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $captureposarg := 163;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 163,
        name => 'captureposarg')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $captureposarg_i := 164;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 164,
        name => 'captureposarg_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $captureposarg_n := 165;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 165,
        name => 'captureposarg_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $captureposarg_s := 166;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 166,
        name => 'captureposarg_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $captureposprimspec := 167;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 167,
        name => 'captureposprimspec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $invokewithcapture := 168;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 168,
        name => 'invokewithcapture')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $multicacheadd := 169;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 169,
        name => 'multicacheadd')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $multicachefind := 170;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 170,
        name => 'multicachefind')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $lexprimspec := 171;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 171,
        name => 'lexprimspec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $ceil_n := 172;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 172,
        name => 'ceil_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $floor_n := 173;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 173,
        name => 'floor_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $assign := 174;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 174,
        name => 'assign')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $assignunchecked := 175;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 175,
        name => 'assignunchecked')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $objprimspec := 176;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 176,
        name => 'objprimspec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $backtracestrings := 177;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 177,
        name => 'backtracestrings')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $masttofile := 178;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 178,
        name => 'masttofile')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $masttocu := 179;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 179,
        name => 'masttocu')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $iscompunit := 180;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 180,
        name => 'iscompunit')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $compunitmainline := 181;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 181,
        name => 'compunitmainline')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $compunitcodes := 182;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 182,
        name => 'compunitcodes')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $sleep := 183;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 183,
        name => 'sleep')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $say_I := 184;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 184,
        name => 'say_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $concat_s := 185;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 185,
        name => 'concat_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $repeat_s := 186;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 186,
        name => 'repeat_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $substr_s := 187;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 187,
        name => 'substr_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $index_s := 188;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 188,
        name => 'index_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $graphs_s := 189;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 189,
        name => 'graphs_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $codes_s := 190;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 190,
        name => 'codes_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $eq_s := 191;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 191,
        name => 'eq_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $ne_s := 192;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 192,
        name => 'ne_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $eqat_s := 193;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 193,
        name => 'eqat_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $haveat_s := 194;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 194,
        name => 'haveat_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $getcp_s := 195;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 195,
        name => 'getcp_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $indexcp_s := 196;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 196,
        name => 'indexcp_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $uc := 197;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 197,
        name => 'uc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $lc := 198;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 198,
        name => 'lc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $tc := 199;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 199,
        name => 'tc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $buftostr := 200;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 200,
        name => 'buftostr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $strtobuf := 201;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 201,
        name => 'strtobuf')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $decode_s := 202;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 202,
        name => 'decode_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $decode_b := 203;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 203,
        name => 'decode_b')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $decode := 204;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 204,
        name => 'decode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $encode := 205;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 205,
        name => 'encode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $split := 206;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 206,
        name => 'split')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $join := 207;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 207,
        name => 'join')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $replace := 208;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 208,
        name => 'replace')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getcpbyname := 209;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 209,
        name => 'getcpbyname')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $indexat_scb := 210;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 210,
        name => 'indexat_scb')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_str,
        $MVM_operand_ins
    ));
    our $unipropcode := 211;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 211,
        name => 'unipropcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $unipvalcode := 212;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 212,
        name => 'unipvalcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $hasuniprop := 213;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 213,
        name => 'hasuniprop')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $hasunipropc := 214;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 214,
        name => 'hasunipropc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_int16,
        $MVM_operand_int16
    ));
    our $concatr_s := 215;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 215,
        name => 'concatr_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $splice_s := 216;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 216,
        name => 'splice_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $chars := 217;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 217,
        name => 'chars')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $chr := 218;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 218,
        name => 'chr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $ordfirst := 219;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 219,
        name => 'ordfirst')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $ordat := 220;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 220,
        name => 'ordat')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $rindexfrom := 221;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 221,
        name => 'rindexfrom')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $escape := 222;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 222,
        name => 'escape')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $flip := 223;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 223,
        name => 'flip')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $iscclass := 224;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 224,
        name => 'iscclass')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $findcclass := 225;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 225,
        name => 'findcclass')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $findnotcclass := 226;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 226,
        name => 'findnotcclass')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $nfafromstatelist := 227;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 227,
        name => 'nfafromstatelist')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $nfarunproto := 228;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 228,
        name => 'nfarunproto')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $nfarunalt := 229;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 229,
        name => 'nfarunalt')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $flattenropes := 230;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 230,
        name => 'flattenropes')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $gt_s := 231;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 231,
        name => 'gt_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $ge_s := 232;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 232,
        name => 'ge_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $lt_s := 233;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 233,
        name => 'lt_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $le_s := 234;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 234,
        name => 'le_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $cmp_s := 235;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 235,
        name => 'cmp_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $radix := 236;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 236,
        name => 'radix')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $eqatic_s := 237;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 237,
        name => 'eqatic_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $sin_n := 238;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 238,
        name => 'sin_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $asin_n := 239;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 239,
        name => 'asin_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $cos_n := 240;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 240,
        name => 'cos_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $acos_n := 241;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 241,
        name => 'acos_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $tan_n := 242;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 242,
        name => 'tan_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $atan_n := 243;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 243,
        name => 'atan_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $atan2_n := 244;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 244,
        name => 'atan2_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_write_reg +| $MVM_operand_num64
    ));
    our $sec_n := 245;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 245,
        name => 'sec_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $asec_n := 246;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 246,
        name => 'asec_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $sinh_n := 247;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 247,
        name => 'sinh_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $cosh_n := 248;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 248,
        name => 'cosh_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $tanh_n := 249;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 249,
        name => 'tanh_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $sech_n := 250;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 250,
        name => 'sech_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $sqrt_n := 251;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 251,
        name => 'sqrt_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $gcd_i := 252;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 252,
        name => 'gcd_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $lcm_i := 253;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 253,
        name => 'lcm_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $add_I := 254;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 254,
        name => 'add_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $sub_I := 255;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 255,
        name => 'sub_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $mul_I := 256;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 256,
        name => 'mul_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $div_I := 257;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 257,
        name => 'div_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $mod_I := 258;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 258,
        name => 'mod_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $neg_I := 259;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 259,
        name => 'neg_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $abs_I := 260;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 260,
        name => 'abs_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $inc_I := 261;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 261,
        name => 'inc_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $dec_I := 262;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 262,
        name => 'dec_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $cmp_I := 263;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 263,
        name => 'cmp_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $eq_I := 264;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 264,
        name => 'eq_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ne_I := 265;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 265,
        name => 'ne_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $lt_I := 266;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 266,
        name => 'lt_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $le_I := 267;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 267,
        name => 'le_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $gt_I := 268;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 268,
        name => 'gt_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ge_I := 269;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 269,
        name => 'ge_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $not_I := 270;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 270,
        name => 'not_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bor_I := 271;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 271,
        name => 'bor_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bxor_I := 272;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 272,
        name => 'bxor_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $band_I := 273;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 273,
        name => 'band_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bnot_I := 274;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 274,
        name => 'bnot_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $blshift_I := 275;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 275,
        name => 'blshift_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $brshift_I := 276;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 276,
        name => 'brshift_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $pow_I := 277;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 277,
        name => 'pow_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $gcd_I := 278;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 278,
        name => 'gcd_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $lcm_I := 279;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 279,
        name => 'lcm_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $expmod_I := 280;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 280,
        name => 'expmod_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isprime_I := 281;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 281,
        name => 'isprime_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $rand_I := 282;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 282,
        name => 'rand_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_Ii := 283;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 283,
        name => 'coerce_Ii')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_In := 284;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 284,
        name => 'coerce_In')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_Is := 285;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 285,
        name => 'coerce_Is')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_iI := 286;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 286,
        name => 'coerce_iI')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_nI := 287;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 287,
        name => 'coerce_nI')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $coerce_sI := 288;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 288,
        name => 'coerce_sI')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isbig_I := 289;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 289,
        name => 'isbig_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $base_I := 290;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 290,
        name => 'base_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $radix_I := 291;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 291,
        name => 'radix_I')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $div_In := 292;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 292,
        name => 'div_In')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $log_n := 293;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 293,
        name => 'log_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $exp_n := 294;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 294,
        name => 'exp_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $knowhow := 295;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 295,
        name => 'knowhow')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $findmeth := 296;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 296,
        name => 'findmeth')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str
    ));
    our $findmeth_s := 297;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 297,
        name => 'findmeth_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $can := 298;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 298,
        name => 'can')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str
    ));
    our $can_s := 299;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 299,
        name => 'can_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $create := 300;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 300,
        name => 'create')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $gethow := 301;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 301,
        name => 'gethow')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getwhat := 302;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 302,
        name => 'getwhat')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $atkey_i := 303;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 303,
        name => 'atkey_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $atkey_n := 304;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 304,
        name => 'atkey_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $atkey_s := 305;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 305,
        name => 'atkey_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $atkey_o := 306;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 306,
        name => 'atkey_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindkey_i := 307;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 307,
        name => 'bindkey_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bindkey_n := 308;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 308,
        name => 'bindkey_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $bindkey_s := 309;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 309,
        name => 'bindkey_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindkey_o := 310;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 310,
        name => 'bindkey_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $existskey := 311;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 311,
        name => 'existskey')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $deletekey := 312;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 312,
        name => 'deletekey')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getwhere := 313;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 313,
        name => 'getwhere')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $eqaddr := 314;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 314,
        name => 'eqaddr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $reprname := 315;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 315,
        name => 'reprname')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isconcrete := 316;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 316,
        name => 'isconcrete')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $atpos_i := 317;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 317,
        name => 'atpos_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $atpos_n := 318;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 318,
        name => 'atpos_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $atpos_s := 319;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 319,
        name => 'atpos_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $atpos_o := 320;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 320,
        name => 'atpos_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bindpos_i := 321;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 321,
        name => 'bindpos_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bindpos_n := 322;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 322,
        name => 'bindpos_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $bindpos_s := 323;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 323,
        name => 'bindpos_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindpos_o := 324;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 324,
        name => 'bindpos_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $push_i := 325;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 325,
        name => 'push_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $push_n := 326;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 326,
        name => 'push_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $push_s := 327;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 327,
        name => 'push_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $push_o := 328;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 328,
        name => 'push_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $pop_i := 329;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 329,
        name => 'pop_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $pop_n := 330;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 330,
        name => 'pop_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $pop_s := 331;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 331,
        name => 'pop_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $pop_o := 332;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 332,
        name => 'pop_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $shift_i := 333;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 333,
        name => 'shift_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $shift_n := 334;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 334,
        name => 'shift_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $shift_s := 335;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 335,
        name => 'shift_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $shift_o := 336;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 336,
        name => 'shift_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $unshift_i := 337;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 337,
        name => 'unshift_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $unshift_n := 338;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 338,
        name => 'unshift_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $unshift_s := 339;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 339,
        name => 'unshift_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $unshift_o := 340;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 340,
        name => 'unshift_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $splice := 341;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 341,
        name => 'splice')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $setelemspos := 342;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 342,
        name => 'setelemspos')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $box_i := 343;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 343,
        name => 'box_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $box_n := 344;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 344,
        name => 'box_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $box_s := 345;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 345,
        name => 'box_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $unbox_i := 346;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 346,
        name => 'unbox_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $unbox_n := 347;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 347,
        name => 'unbox_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $unbox_s := 348;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 348,
        name => 'unbox_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bindattr_i := 349;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 349,
        name => 'bindattr_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_int16
    ));
    our $bindattr_n := 350;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 350,
        name => 'bindattr_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_num64,
        $MVM_operand_int16
    ));
    our $bindattr_s := 351;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 351,
        name => 'bindattr_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $bindattr_o := 352;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 352,
        name => 'bindattr_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_int16
    ));
    our $bindattrs_i := 353;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 353,
        name => 'bindattrs_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $bindattrs_n := 354;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 354,
        name => 'bindattrs_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_num64
    ));
    our $bindattrs_s := 355;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 355,
        name => 'bindattrs_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindattrs_o := 356;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 356,
        name => 'bindattrs_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getattr_i := 357;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 357,
        name => 'getattr_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $getattr_n := 358;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 358,
        name => 'getattr_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $getattr_s := 359;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 359,
        name => 'getattr_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $getattr_o := 360;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 360,
        name => 'getattr_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_str,
        $MVM_operand_int16
    ));
    our $getattrs_i := 361;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 361,
        name => 'getattrs_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getattrs_n := 362;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 362,
        name => 'getattrs_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getattrs_s := 363;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 363,
        name => 'getattrs_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getattrs_o := 364;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 364,
        name => 'getattrs_o')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $isnull := 365;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 365,
        name => 'isnull')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $knowhowattr := 366;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 366,
        name => 'knowhowattr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $iscoderef := 367;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 367,
        name => 'iscoderef')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $null := 368;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 368,
        name => 'null')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $clone := 369;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 369,
        name => 'clone')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isnull_s := 370;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 370,
        name => 'isnull_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bootint := 371;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 371,
        name => 'bootint')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootnum := 372;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 372,
        name => 'bootnum')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootstr := 373;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 373,
        name => 'bootstr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootarray := 374;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 374,
        name => 'bootarray')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $boothash := 375;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 375,
        name => 'boothash')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $sethllconfig := 376;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 376,
        name => 'sethllconfig')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $hllboxtype_i := 377;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 377,
        name => 'hllboxtype_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $hllboxtype_n := 378;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 378,
        name => 'hllboxtype_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $hllboxtype_s := 379;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 379,
        name => 'hllboxtype_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $elems := 380;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 380,
        name => 'elems')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $null_s := 381;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 381,
        name => 'null_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str
    ));
    our $newtype := 382;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 382,
        name => 'newtype')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $islist := 383;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 383,
        name => 'islist')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ishash := 384;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 384,
        name => 'ishash')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $iter := 385;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 385,
        name => 'iter')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $iterkey_s := 386;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 386,
        name => 'iterkey_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $iterval := 387;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 387,
        name => 'iterval')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getcodename := 388;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 388,
        name => 'getcodename')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $composetype := 389;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 389,
        name => 'composetype')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setmethcache := 390;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 390,
        name => 'setmethcache')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setmethcacheauth := 391;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 391,
        name => 'setmethcacheauth')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $settypecache := 392;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 392,
        name => 'settypecache')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setinvokespec := 393;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 393,
        name => 'setinvokespec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isinvokable := 394;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 394,
        name => 'isinvokable')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $iscont := 395;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 395,
        name => 'iscont')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $decont := 396;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 396,
        name => 'decont')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setboolspec := 397;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 397,
        name => 'setboolspec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $istrue := 398;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 398,
        name => 'istrue')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $isfalse := 399;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 399,
        name => 'isfalse')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $istrue_s := 400;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 400,
        name => 'istrue_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $isfalse_s := 401;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 401,
        name => 'isfalse_s')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getcodeobj := 402;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 402,
        name => 'getcodeobj')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setcodeobj := 403;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 403,
        name => 'setcodeobj')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setcodename := 404;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 404,
        name => 'setcodename')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $forceouterctx := 405;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 405,
        name => 'forceouterctx')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getcomp := 406;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 406,
        name => 'getcomp')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindcomp := 407;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 407,
        name => 'bindcomp')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getcurhllsym := 408;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 408,
        name => 'getcurhllsym')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $bindcurhllsym := 409;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 409,
        name => 'bindcurhllsym')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getwho := 410;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 410,
        name => 'getwho')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setwho := 411;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 411,
        name => 'setwho')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $rebless := 412;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 412,
        name => 'rebless')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $istype := 413;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 413,
        name => 'istype')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ctx := 414;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 414,
        name => 'ctx')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $ctxouter := 415;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 415,
        name => 'ctxouter')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ctxcaller := 416;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 416,
        name => 'ctxcaller')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $ctxlexpad := 417;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 417,
        name => 'ctxlexpad')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $curcode := 418;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 418,
        name => 'curcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $callercode := 419;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 419,
        name => 'callercode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootintarray := 420;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 420,
        name => 'bootintarray')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootnumarray := 421;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 421,
        name => 'bootnumarray')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $bootstrarray := 422;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 422,
        name => 'bootstrarray')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $hlllist := 423;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 423,
        name => 'hlllist')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $hllhash := 424;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 424,
        name => 'hllhash')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $attrinited := 425;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 425,
        name => 'attrinited')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $setcontspec := 426;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 426,
        name => 'setcontspec')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $existspos := 427;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 427,
        name => 'existspos')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $gethllsym := 428;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 428,
        name => 'gethllsym')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $freshcoderef := 429;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 429,
        name => 'freshcoderef')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $markcodestatic := 430;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 430,
        name => 'markcodestatic')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $markcodestub := 431;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 431,
        name => 'markcodestub')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getstaticcode := 432;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 432,
        name => 'getstaticcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getcodecuid := 433;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 433,
        name => 'getcodecuid')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $copy_f := 434;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 434,
        name => 'copy_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $append_f := 435;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 435,
        name => 'append_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $rename_f := 436;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 436,
        name => 'rename_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $delete_f := 437;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 437,
        name => 'delete_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $chmod_f := 438;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 438,
        name => 'chmod_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $exists_f := 439;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 439,
        name => 'exists_f')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $mkdir := 440;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 440,
        name => 'mkdir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $rmdir := 441;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 441,
        name => 'rmdir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $open_dir := 442;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 442,
        name => 'open_dir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $read_dir := 443;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 443,
        name => 'read_dir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $close_dir := 444;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 444,
        name => 'close_dir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $open_fh := 445;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 445,
        name => 'open_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $close_fh := 446;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 446,
        name => 'close_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $read_fhs := 447;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 447,
        name => 'read_fhs')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $read_fhbuf := 448;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 448,
        name => 'read_fhbuf')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $slurp := 449;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 449,
        name => 'slurp')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $spew := 450;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 450,
        name => 'spew')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $write_fhs := 451;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 451,
        name => 'write_fhs')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $write_fhbuf := 452;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 452,
        name => 'write_fhbuf')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $seek_fh := 453;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 453,
        name => 'seek_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $lock_fh := 454;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 454,
        name => 'lock_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $unlock_fh := 455;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 455,
        name => 'unlock_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $sync_fh := 456;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 456,
        name => 'sync_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $trunc_fh := 457;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 457,
        name => 'trunc_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $eof_fh := 458;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 458,
        name => 'eof_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getstdin := 459;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 459,
        name => 'getstdin')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $getstdout := 460;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 460,
        name => 'getstdout')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $getstderr := 461;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 461,
        name => 'getstderr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $connect_sk := 462;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 462,
        name => 'connect_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $close_sk := 463;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 463,
        name => 'close_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $bind_sk := 464;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 464,
        name => 'bind_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $listen_sk := 465;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 465,
        name => 'listen_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $accept_sk := 466;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 466,
        name => 'accept_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $send_sks := 467;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 467,
        name => 'send_sks')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $send_skbuf := 468;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 468,
        name => 'send_skbuf')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $recv_sks := 469;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 469,
        name => 'recv_sks')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $recv_skbuf := 470;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 470,
        name => 'recv_skbuf')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $getaddr_sk := 471;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 471,
        name => 'getaddr_sk')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $nametoaddr := 472;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 472,
        name => 'nametoaddr')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $addrtoname := 473;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 473,
        name => 'addrtoname')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $porttosvc := 474;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 474,
        name => 'porttosvc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $setencoding := 475;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 475,
        name => 'setencoding')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $print := 476;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 476,
        name => 'print')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $say := 477;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 477,
        name => 'say')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $readall_fh := 478;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 478,
        name => 'readall_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $tell_fh := 479;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 479,
        name => 'tell_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $stat := 480;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 480,
        name => 'stat')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $readline_fh := 481;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 481,
        name => 'readline_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $readlineint_fh := 482;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 482,
        name => 'readlineint_fh')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $procshell := 483;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 483,
        name => 'procshell')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $procshellbg := 484;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 484,
        name => 'procshellbg')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $procrun := 485;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 485,
        name => 'procrun')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $procrunbg := 486;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 486,
        name => 'procrunbg')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $prockill := 487;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 487,
        name => 'prockill')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $procwait := 488;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 488,
        name => 'procwait')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $procalive := 489;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 489,
        name => 'procalive')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $detach := 490;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 490,
        name => 'detach')
    );
    nqp::push($operands_list, nqp::list_i());
    our $daemonize := 491;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 491,
        name => 'daemonize')
    );
    nqp::push($operands_list, nqp::list_i());
    our $chdir := 492;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 492,
        name => 'chdir')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $rand_i := 493;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 493,
        name => 'rand_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $rand_n := 494;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 494,
        name => 'rand_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64
    ));
    our $time_i := 495;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 495,
        name => 'time_i')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64
    ));
    our $clargs := 496;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 496,
        name => 'clargs')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $newthread := 497;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 497,
        name => 'newthread')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $jointhread := 498;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 498,
        name => 'jointhread')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $time_n := 499;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 499,
        name => 'time_n')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_num64
    ));
    our $exit := 500;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 500,
        name => 'exit')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $loadbytecode := 501;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 501,
        name => 'loadbytecode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $getenvhash := 502;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 502,
        name => 'getenvhash')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $compilemasttofile := 503;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 503,
        name => 'compilemasttofile')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $sha1 := 504;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 504,
        name => 'sha1')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $createsc := 505;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 505,
        name => 'createsc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $scsetobj := 506;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 506,
        name => 'scsetobj')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $scsetcode := 507;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 507,
        name => 'scsetcode')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $scgetobj := 508;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 508,
        name => 'scgetobj')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_int64
    ));
    our $scgethandle := 509;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 509,
        name => 'scgethandle')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $scgetobjidx := 510;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 510,
        name => 'scgetobjidx')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $scsetdesc := 511;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 511,
        name => 'scsetdesc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_str
    ));
    our $scobjcount := 512;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 512,
        name => 'scobjcount')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_int64,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $setobjsc := 513;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 513,
        name => 'setobjsc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $getobjsc := 514;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 514,
        name => 'getobjsc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $serialize := 515;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 515,
        name => 'serialize')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $deserialize := 516;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 516,
        name => 'deserialize')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $wval := 517;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 517,
        name => 'wval')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int16,
        $MVM_operand_int16
    ));
    our $wval_wide := 518;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 518,
        name => 'wval_wide')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj,
        $MVM_operand_int16,
        $MVM_operand_int64
    ));
    our $scwbdisable := 519;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 519,
        name => 'scwbdisable')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $scwbenable := 520;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 520,
        name => 'scwbenable')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $pushcompsc := 521;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 521,
        name => 'pushcompsc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
    our $popcompsc := 522;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 522,
        name => 'popcompsc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_obj
    ));
    our $scgetdesc := 523;
    nqp::push($ops_list, MAST::OpCode.new(
        code => 523,
        name => 'scgetdesc')
    );
    nqp::push($operands_list, nqp::list_i(
        $MVM_operand_write_reg +| $MVM_operand_str,
        $MVM_operand_read_reg +| $MVM_operand_obj
    ));
}
