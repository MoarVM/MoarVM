#include "moarvm.h"
#include "math.h"

/* Macros for getting things from the bytecode stream. */
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_I64(pc, idx)    *((MVMint64 *)(pc + idx))
#define GET_UI64(pc, idx)   *((MVMuint64 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))
#define GET_N64(pc, idx)    *((MVMnum64 *)(pc + idx))

/* This is the interpreter run loop. We have one of these per thread. */
void MVM_interp_run(MVMThreadContext *tc, MVMFrame *initial_frame) {
    /* Points to the current opcode. */
    MVMuint8 *cur_op = initial_frame->static_info->bytecode;
    
    /* The current frame's bytecode start. */
    MVMuint8 *bytecode_start = initial_frame->static_info->bytecode;
    
    /* Points to the base of the current register set for the frame we
     * are presently in. */
    MVMRegister *reg_base = initial_frame->work;
    
    /* Points to the current compilation unit. */
    MVMCompUnit *cu = initial_frame->static_info->cu;

    /* Points to the base of the current pre-deref'd SC object set for the
     * compilation unit we're running in. */
    MVMObject *sc_deref_base; /* XXX set... */
    
    /* Stash addresses of current op, register base and SC deref base
     * in the TC; this will be used by anything that needs to switch
     * the current place we're interpreting. */
    tc->interp_cur_op        = &cur_op;
    tc->interp_reg_base      = &reg_base;
    tc->interp_sc_deref_base = &sc_deref_base;
    
    /* Enter runloop. */
    while (1) {
        /* Primary dispatch by op bank. */
        switch (*(cur_op++)) {
            /* Control flow and primitive operations. */
            case MVM_OP_BANK_primitives: {
                switch (*(cur_op++)) {
                    case MVM_OP_no_op:
                        break;
                    case MVM_OP_goto:
                        cur_op = bytecode_start + GET_UI32(cur_op, 0);
                        break;
                    case MVM_OP_if_i:
                        if (GET_REG(cur_op, 0).i64)
                            cur_op = bytecode_start + GET_UI32(cur_op, 2);
                        else
                            cur_op += 6;
                        break;
                    case MVM_OP_unless_i:
                        if (GET_REG(cur_op, 0).i64)
                            cur_op += 6;
                        else
                            cur_op = bytecode_start + GET_UI32(cur_op, 2);
                        break;
                    case MVM_OP_if_n:
                        if (GET_REG(cur_op, 0).n64 != 0.0)
                            cur_op = bytecode_start + GET_UI32(cur_op, 2);
                        else
                            cur_op += 6;
                        break;
                    case MVM_OP_unless_n:
                        if (GET_REG(cur_op, 0).n64 != 0.0)
                            cur_op += 6;
                        else
                            cur_op = bytecode_start + GET_UI32(cur_op, 2);
                        break;
                    case MVM_OP_return:
                        return;
                    case MVM_OP_const_i64:
                        GET_REG(cur_op, 0).i64 = GET_I64(cur_op, 2);
                        cur_op += 10;
                        break;
                    case MVM_OP_const_n64:
                        GET_REG(cur_op, 0).n64 = GET_N64(cur_op, 2);
                        cur_op += 10;
                        break;
                    case MVM_OP_const_s:
                        GET_REG(cur_op, 0).s = cu->strings[GET_UI16(cur_op, 2)];
                        cur_op += 4;
                        break;
                    case MVM_OP_add_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 + GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_sub_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 - GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_mul_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 * GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_div_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 / GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_div_u:
                        GET_REG(cur_op, 0).ui64 = GET_REG(cur_op, 2).ui64 / GET_REG(cur_op, 4).ui64;
                        cur_op += 6;
                        break;
                    case MVM_OP_neg_i:
                        GET_REG(cur_op, 0).i64 = -GET_REG(cur_op, 2).i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_inc_i:
                        GET_REG(cur_op, 0).i64++;
                        cur_op += 2;
                        break;
                    case MVM_OP_inc_u:
                        GET_REG(cur_op, 0).ui64++;
                        cur_op += 2;
                        break;
                    case MVM_OP_dec_i:
                        GET_REG(cur_op, 0).i64--;
                        cur_op += 2;
                        break;
                    case MVM_OP_dec_u:
                        GET_REG(cur_op, 0).ui64--;
                        cur_op += 2;
                        break;
                    case MVM_OP_getcode:
                        GET_REG(cur_op, 0).o = cu->coderefs[GET_UI16(cur_op, 2)];
                        cur_op += 4;
                        break;
                    case MVM_OP_invoke_v:
                        {
                            MVMObject *code = GET_REG(cur_op, 0).o;
                            cur_op += 2;
                            /* XXX Fill in callframe, args. */
                            STABLE(code)->invoke(tc, code, NULL, NULL);
                        }
                        break;
                    case MVM_OP_add_n:
                        GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 + GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_sub_n:
                        GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 - GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_mul_n:
                        GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 * GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_div_n:
                        GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 / GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_neg_n:
                        GET_REG(cur_op, 0).n64 = -GET_REG(cur_op, 2).n64;
                        cur_op += 4;
                        break;
                    case MVM_OP_eq_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 == GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_ne_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 != GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_lt_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <  GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_le_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <= GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_gt_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >  GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_ge_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >= GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_eq_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 == GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_ne_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 != GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_lt_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 <  GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_le_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 <= GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_gt_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >  GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    case MVM_OP_ge_n:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >= GET_REG(cur_op, 4).n64;
                        cur_op += 6;
                        break;
                    default: {
                        MVM_panic("Invalid opcode executed (corrupt bytecode stream?)");
                    }
                    break;
                }
            }
            break;
            
            /* Development operations. */
            case MVM_OP_BANK_dev: {
                switch (*(cur_op++)) {
                    case MVM_OP_say_i:
                        printf("%d\n", GET_REG(cur_op, 0).i64); /* XXX %d is 32-bit only, I guess... */
                        cur_op += 2;
                        break;
                    case MVM_OP_say_n:
                        printf("%f\n", GET_REG(cur_op, 0).n64);
                        cur_op += 2;
                        break;
                    case MVM_OP_say_s:
                        MVM_string_say(tc, GET_REG(cur_op, 0).s);
                        /*printf("%s\n", MVM_string_ascii_encode(tc,
                            GET_REG(cur_op, 0).s, NULL));*/
                        cur_op += 2;
                        break;
                    default: {
                        MVM_panic("Invalid opcode executed (corrupt bytecode stream?)");
                    }
                    break;
                }
            }
            break;
            
            /* String operations. */
            case MVM_OP_BANK_string: {
                switch (*(cur_op++)) {
                    case MVM_OP_concat_s:
                        GET_REG(cur_op, 0).s = MVM_string_concatenate(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_repeat_s:
                        GET_REG(cur_op, 0).s = MVM_string_repeat(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_substr_s:
                        GET_REG(cur_op, 0).s = MVM_string_substring(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64,
                            GET_REG(cur_op, 6).i64);
                        cur_op += 8;
                        break;
                    case MVM_OP_index_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_index(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_graphs_s:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).s->body.graphs;
                        cur_op += 4;
                        break;
                    case MVM_OP_codes_s:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).s->body.codes;
                        cur_op += 4;
                        break;
                    case MVM_OP_eq_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_equal(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_ne_s:
                        GET_REG(cur_op, 0).i64 = (MVMint64)(MVM_string_equal(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s)?0:1);
                        cur_op += 6;
                        break;
                    case MVM_OP_startsw_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_starts_with(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_endsw_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_ends_with(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_isat_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_is_at(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                            GET_REG(cur_op, 6).i64);
                        cur_op += 8;
                        break;
                    default: {
                        MVM_panic("Invalid opcode executed (corrupt bytecode stream?)");
                    }
                    break;
                }
            }
            break;
            
            /* Math operations other than the primitives. */
            case MVM_OP_BANK_math: {
                switch (*(cur_op++)) {
                    case MVM_OP_sin_n:
                        GET_REG(cur_op, 0).n64 = sin(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_asin_n:
                        GET_REG(cur_op, 0).n64 = asin(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_cos_n:
                        GET_REG(cur_op, 0).n64 = cos(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_acos_n:
                        GET_REG(cur_op, 0).n64 = acos(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_tan_n:
                        GET_REG(cur_op, 0).n64 = tan(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_atan_n:
                        GET_REG(cur_op, 0).n64 = atan(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_atan2_n:
                        GET_REG(cur_op, 0).n64 = atan2(GET_REG(cur_op, 2).n64,
                            GET_REG(cur_op, 4).n64);
                        cur_op += 6;
                        break;
                    case MVM_OP_sec_n: /* XXX TODO: handle edge cases */
                        GET_REG(cur_op, 0).n64 = 1.0 / cos(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_asec_n: /* XXX TODO: handle edge cases */
                        GET_REG(cur_op, 0).n64 = acos(1.0 / GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_sinh_n:
                        GET_REG(cur_op, 0).n64 = sinh(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_cosh_n:
                        GET_REG(cur_op, 0).n64 = cosh(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_tanh_n:
                        GET_REG(cur_op, 0).n64 = tanh(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    case MVM_OP_sech_n: /* XXX TODO: handle edge cases */
                        GET_REG(cur_op, 0).n64 = 1.0 / cosh(GET_REG(cur_op, 2).n64);
                        cur_op += 4;
                        break;
                    default: {
                        MVM_panic("Invalid opcode executed (corrupt bytecode stream?)");
                    }
                    break;
                }
            }
            break;
            
            /* Dispatch to bank function. */
            default:
            {
                MVM_panic("Invalid opcode executed (corrupt bytecode stream?)");
            }
            break;
        }
    }
}
