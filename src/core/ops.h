/* This file is generated from src/core/oplist by tools/update_ops_h.p6. */

/* Bank name defines. */
#define MVM_OP_BANK_primitives 0

/* Op name defines for bank primitives. */
#define MVM_OP_no_op 0
#define MVM_OP_goto 1
#define MVM_OP_if_i 2
#define MVM_OP_unless_i 3
#define MVM_OP_if_n 4
#define MVM_OP_unless_n 5
#define MVM_OP_if_s 6
#define MVM_OP_unless_s 7
#define MVM_OP_if_s0 8
#define MVM_OP_unless_s0 9
#define MVM_OP_if_o 10
#define MVM_OP_unless_o 11
#define MVM_OP_set 12
#define MVM_OP_extend_u8 13
#define MVM_OP_extend_u16 14
#define MVM_OP_extend_u32 15
#define MVM_OP_extend_i8 16
#define MVM_OP_extend_i16 17
#define MVM_OP_extend_i32 18
#define MVM_OP_trunc_u8 19
#define MVM_OP_trunc_u16 20
#define MVM_OP_trunc_u32 21
#define MVM_OP_trunc_i8 22
#define MVM_OP_trunc_i16 23
#define MVM_OP_trunc_i32 24
#define MVM_OP_extend_n32 25
#define MVM_OP_trunc_n32 26
#define MVM_OP_get_lex 27
#define MVM_OP_bind_lex 28
#define MVM_OP_get_lex_lo 29
#define MVM_OP_bind_lex_lo 30
#define MVM_OP_get_lex_ni 31
#define MVM_OP_get_lex_nn 32
#define MVM_OP_get_lex_ns 33
#define MVM_OP_get_lex_no 34
#define MVM_OP_bind_lex_ni 35
#define MVM_OP_bind_lex_nn 36
#define MVM_OP_bind_lex_ns 37
#define MVM_OP_bind_lex_no 38
#define MVM_OP_return_i 39
#define MVM_OP_return_n 40
#define MVM_OP_return_s 41
#define MVM_OP_return_o 42
#define MVM_OP_return 43

static MVMOpInfo MVM_op_info_primitives[] = {
    {
        MVM_OP_no_op,
        "no_op"
    },
    {
        MVM_OP_goto,
        "goto"
    },
    {
        MVM_OP_if_i,
        "if_i"
    },
    {
        MVM_OP_unless_i,
        "unless_i"
    },
    {
        MVM_OP_if_n,
        "if_n"
    },
    {
        MVM_OP_unless_n,
        "unless_n"
    },
    {
        MVM_OP_if_s,
        "if_s"
    },
    {
        MVM_OP_unless_s,
        "unless_s"
    },
    {
        MVM_OP_if_s0,
        "if_s0"
    },
    {
        MVM_OP_unless_s0,
        "unless_s0"
    },
    {
        MVM_OP_if_o,
        "if_o"
    },
    {
        MVM_OP_unless_o,
        "unless_o"
    },
    {
        MVM_OP_set,
        "set"
    },
    {
        MVM_OP_extend_u8,
        "extend_u8"
    },
    {
        MVM_OP_extend_u16,
        "extend_u16"
    },
    {
        MVM_OP_extend_u32,
        "extend_u32"
    },
    {
        MVM_OP_extend_i8,
        "extend_i8"
    },
    {
        MVM_OP_extend_i16,
        "extend_i16"
    },
    {
        MVM_OP_extend_i32,
        "extend_i32"
    },
    {
        MVM_OP_trunc_u8,
        "trunc_u8"
    },
    {
        MVM_OP_trunc_u16,
        "trunc_u16"
    },
    {
        MVM_OP_trunc_u32,
        "trunc_u32"
    },
    {
        MVM_OP_trunc_i8,
        "trunc_i8"
    },
    {
        MVM_OP_trunc_i16,
        "trunc_i16"
    },
    {
        MVM_OP_trunc_i32,
        "trunc_i32"
    },
    {
        MVM_OP_extend_n32,
        "extend_n32"
    },
    {
        MVM_OP_trunc_n32,
        "trunc_n32"
    },
    {
        MVM_OP_get_lex,
        "get_lex"
    },
    {
        MVM_OP_bind_lex,
        "bind_lex"
    },
    {
        MVM_OP_get_lex_lo,
        "get_lex_lo"
    },
    {
        MVM_OP_bind_lex_lo,
        "bind_lex_lo"
    },
    {
        MVM_OP_get_lex_ni,
        "get_lex_ni"
    },
    {
        MVM_OP_get_lex_nn,
        "get_lex_nn"
    },
    {
        MVM_OP_get_lex_ns,
        "get_lex_ns"
    },
    {
        MVM_OP_get_lex_no,
        "get_lex_no"
    },
    {
        MVM_OP_bind_lex_ni,
        "bind_lex_ni"
    },
    {
        MVM_OP_bind_lex_nn,
        "bind_lex_nn"
    },
    {
        MVM_OP_bind_lex_ns,
        "bind_lex_ns"
    },
    {
        MVM_OP_bind_lex_no,
        "bind_lex_no"
    },
    {
        MVM_OP_return_i,
        "return_i"
    },
    {
        MVM_OP_return_n,
        "return_n"
    },
    {
        MVM_OP_return_s,
        "return_s"
    },
    {
        MVM_OP_return_o,
        "return_o"
    },
    {
        MVM_OP_return,
        "return"
    },
};
