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
void MVM_interp_run(MVMThreadContext *tc, struct _MVMStaticFrame *initial_static_frame) {
    /* Points to the current opcode. */
    MVMuint8 *cur_op = NULL;
    
    /* The current frame's bytecode start. */
    MVMuint8 *bytecode_start = NULL;
    
    /* Points to the base of the current register set for the frame we
     * are presently in. */
    MVMRegister *reg_base = NULL;
    
    /* Points to the current compilation unit. */
    MVMCompUnit *cu = NULL;
    
    /* The current call site we're constructing. */
    MVMCallsite *cur_callsite = NULL;
    
    /* Dummy, 0-arg callsite. */
    MVMCallsite no_arg_callsite;
    no_arg_callsite.arg_flags = NULL;
    no_arg_callsite.arg_count = 0;
    no_arg_callsite.num_pos   = 0;
    
    /* Stash addresses of current op, register base and SC deref base
     * in the TC; this will be used by anything that needs to switch
     * the current place we're interpreting. */
    tc->interp_cur_op         = &cur_op;
    tc->interp_bytecode_start = &bytecode_start;
    tc->interp_reg_base       = &reg_base;
    tc->interp_cu             = &cu;
    
    /* Create initial frame, which sets up all of the interpreter state also. */
    MVM_frame_invoke(tc, initial_static_frame, &no_arg_callsite, NULL);
    
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
                    case MVM_OP_return_i:
                        MVM_args_set_result_int(tc, GET_REG(cur_op, 0).i64,
                            MVM_RETURN_CALLER_FRAME);
                        if (MVM_frame_try_return(tc))
                            break;
                        else
                            return;
                    case MVM_OP_return_n:
                        MVM_args_set_result_num(tc, GET_REG(cur_op, 0).n64,
                            MVM_RETURN_CALLER_FRAME);
                        if (MVM_frame_try_return(tc))
                            break;
                        else
                            return;
                    case MVM_OP_return_s:
                        MVM_args_set_result_str(tc, GET_REG(cur_op, 0).s,
                            MVM_RETURN_CALLER_FRAME);
                        if (MVM_frame_try_return(tc))
                            break;
                        else
                            return;
                    case MVM_OP_return_o:
                        MVM_args_set_result_obj(tc, GET_REG(cur_op, 0).o,
                            MVM_RETURN_CALLER_FRAME);
                        if (MVM_frame_try_return(tc))
                            break;
                        else
                            return;
                    case MVM_OP_return:
                        if (MVM_frame_try_return(tc))
                            break;
                        else
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
                    case MVM_OP_mod_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 % GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_mod_u:
                        GET_REG(cur_op, 0).ui64 = GET_REG(cur_op, 2).ui64 % GET_REG(cur_op, 4).ui64;
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
                    case MVM_OP_prepargs:
                        cur_callsite = cu->callsites[GET_UI16(cur_op, 0)];
                        cur_op += 2;
                        break;
                    case MVM_OP_arg_i:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].i64 = GET_REG(cur_op, 2).i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_arg_n:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].n64 = GET_REG(cur_op, 2).n64;
                        cur_op += 4;
                        break;
                    case MVM_OP_arg_s:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].s = GET_REG(cur_op, 2).s;
                        cur_op += 4;
                        break;
                    case MVM_OP_arg_o:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].o = GET_REG(cur_op, 2).o;
                        cur_op += 4;
                        break;
                    case MVM_OP_invoke_v:
                        {
                            MVMObject *code = GET_REG(cur_op, 0).o;
                            tc->cur_frame->return_value = NULL;
                            tc->cur_frame->return_type = MVM_RETURN_VOID;
                            cur_op += 2;
                            tc->cur_frame->return_address = cur_op;
                            STABLE(code)->invoke(tc, code, cur_callsite, tc->cur_frame->args);
                        }
                        break;
                    case MVM_OP_invoke_i:
                        {
                            MVMObject *code = GET_REG(cur_op, 2).o;
                            tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                            tc->cur_frame->return_type = MVM_RETURN_INT;
                            cur_op += 4;
                            tc->cur_frame->return_address = cur_op;
                            STABLE(code)->invoke(tc, code, cur_callsite, tc->cur_frame->args);
                        }
                        break;
                    case MVM_OP_invoke_n:
                        {
                            MVMObject *code = GET_REG(cur_op, 2).o;
                            tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                            tc->cur_frame->return_type = MVM_RETURN_NUM;
                            cur_op += 4;
                            tc->cur_frame->return_address = cur_op;
                            STABLE(code)->invoke(tc, code, cur_callsite, tc->cur_frame->args);
                        }
                        break;
                    case MVM_OP_invoke_s:
                        {
                            MVMObject *code = GET_REG(cur_op, 2).o;
                            tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                            tc->cur_frame->return_type = MVM_RETURN_STR;
                            cur_op += 4;
                            tc->cur_frame->return_address = cur_op;
                            STABLE(code)->invoke(tc, code, cur_callsite, tc->cur_frame->args);
                        }
                        break;
                    case MVM_OP_invoke_o:
                        {
                            MVMObject *code = GET_REG(cur_op, 2).o;
                            tc->cur_frame->return_value = &GET_REG(cur_op, 0);
                            tc->cur_frame->return_type = MVM_RETURN_OBJ;
                            cur_op += 4;
                            tc->cur_frame->return_address = cur_op;
                            STABLE(code)->invoke(tc, code, cur_callsite, tc->cur_frame->args);
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
                    case MVM_OP_argconst_i:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].i64 = GET_I64(cur_op, 2);
                        cur_op += 10;
                        break;
                    case MVM_OP_argconst_n:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].n64 = GET_N64(cur_op, 2);
                        cur_op += 10;
                        break;
                    case MVM_OP_argconst_s:
                        tc->cur_frame->args[GET_UI16(cur_op, 0)].s = cu->strings[GET_UI16(cur_op, 2)];
                        cur_op += 4;
                        break;
                    case MVM_OP_checkarity:
                        MVM_args_checkarity(tc, &tc->cur_frame->params, GET_UI16(cur_op, 0), GET_UI16(cur_op, 2));
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rp_i:
                        GET_REG(cur_op, 0).i64 = MVM_args_get_pos_int(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_REQUIRED)->i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rp_n:
                        GET_REG(cur_op, 0).n64 = MVM_args_get_pos_num(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_REQUIRED)->n64;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rp_s:
                        GET_REG(cur_op, 0).s = MVM_args_get_pos_str(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_REQUIRED)->s;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rp_o:
                        GET_REG(cur_op, 0).o = MVM_args_get_pos_obj(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_REQUIRED)->o;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_op_i:
                    {
                        MVMRegister *param = MVM_args_get_pos_int(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).i64 = param->i64;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_op_n:
                    {
                        MVMRegister *param = MVM_args_get_pos_num(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).n64 = param->n64;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_op_s:
                    {
                        MVMRegister *param = MVM_args_get_pos_str(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).s = param->s;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_op_o:
                    {
                        MVMRegister *param = MVM_args_get_pos_obj(tc, &tc->cur_frame->params,
                            GET_UI16(cur_op, 2), MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).o = param->o;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_rn_i:
                        GET_REG(cur_op, 0).i64 = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_REQUIRED)->i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rn_n:
                        GET_REG(cur_op, 0).n64 = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_REQUIRED)->n64;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rn_s:
                        GET_REG(cur_op, 0).s = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_REQUIRED)->s;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_rn_o:
                        GET_REG(cur_op, 0).o = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_REQUIRED)->o;
                        cur_op += 4;
                        break;
                    case MVM_OP_param_on_i:
                    {
                        MVMRegister *param = MVM_args_get_named_int(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).i64 = param->i64;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_on_n:
                    {
                        MVMRegister *param = MVM_args_get_named_num(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).n64 = param->n64;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_on_s:
                    {
                        MVMRegister *param = MVM_args_get_named_str(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).s = param->s;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_param_on_o:
                    {
                        MVMRegister *param = MVM_args_get_named_obj(tc, &tc->cur_frame->params,
                            cu->strings[GET_UI16(cur_op, 2)], MVM_ARG_OPTIONAL);
                        if (param) {
                            GET_REG(cur_op, 0).o = param->o;
                            cur_op = bytecode_start + GET_UI32(cur_op, 4);
                        }
                        else {
                            cur_op += 8;
                        }
                        break;
                    }
                    case MVM_OP_coerce_in:
                        GET_REG(cur_op, 0).n64 = (MVMnum64)GET_REG(cur_op, 2).i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_coerce_ni:
                        GET_REG(cur_op, 0).i64 = (MVMint64)GET_REG(cur_op, 2).n64;
                        cur_op += 4;
                        break;
                    case MVM_OP_band_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 & GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_bor_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 | GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_bxor_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 ^ GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_bnot_i:
                        GET_REG(cur_op, 0).i64 = ~GET_REG(cur_op, 2).i64;
                        cur_op += 4;
                        break;
                    case MVM_OP_blshift_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 << GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_brshift_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >> GET_REG(cur_op, 4).i64;
                        cur_op += 6;
                        break;
                    case MVM_OP_pow_i: {
                            MVMint64 base = GET_REG(cur_op, 2).i64;
                            MVMint64 exp = GET_REG(cur_op, 4).i64;
                            MVMint64 result = 1;
                            /* "Exponentiation by squaring" */
                            if (exp < 1) {
                                result = 0; /* because 1/base**-exp is between 0 and 1 */
                            }
                            else {
                                while (exp) {
                                    if (exp & 1)
                                        result *= base;
                                    exp >>= 1;
                                    base *= base;
                                }
                            }
                            GET_REG(cur_op, 0).i64 = result;
                        }
                        cur_op += 6;
                        break;
                    case MVM_OP_pow_n:
                        GET_REG(cur_op, 0).n64 = pow(GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).n64);
                        cur_op += 6;
                        break;
                    default: {
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_primitives, *(cur_op-1));
                    }
                    break;
                }
            }
            break;
            
            /* Development operations. */
            case MVM_OP_BANK_dev: {
                switch (*(cur_op++)) {
                    case MVM_OP_say_i:
                        printf("%lld\n", GET_REG(cur_op, 0).i64); /* lld seems to work on msvc and gcc */
                        cur_op += 2;
                        break;
                    case MVM_OP_say_n:
                        printf("%f\n", GET_REG(cur_op, 0).n64);
                        cur_op += 2;
                        break;
                    case MVM_OP_say_s:
                        MVM_string_say(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_sleep: /* microseconds for now */
                        apr_sleep((apr_interval_time_t)GET_REG(cur_op, 0).i64);
                        cur_op += 2;
                        break;
                    case MVM_OP_anonoshtype:
                        GET_REG(cur_op, 0).o = MVM_file_get_anon_oshandle_type(tc);
                        cur_op += 2;
                        break;
                    default: {
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_dev, *(cur_op-1));
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
                    case MVM_OP_eqat_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_equal_at(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                            GET_REG(cur_op, 6).i64);
                        cur_op += 8;
                        break;
                    case MVM_OP_haveat_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_have_at(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64,
                            GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).s,
                            GET_REG(cur_op, 10).i64);
                        cur_op += 12;
                        break;
                    case MVM_OP_getcp_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_get_codepoint_at(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_setcp_s:
                        MVM_string_set_codepoint_at(tc, GET_REG(cur_op, 0).s,
                            GET_REG(cur_op, 2).i64, GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_indexcp_s:
                        GET_REG(cur_op, 0).i64 = MVM_string_index_of_codepoint(tc,
                            GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    default: {
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_string, *(cur_op-1));
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
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_math, *(cur_op-1));
                    }
                    break;
                }
            }
            break;
            
            /* Object operations. */
            case MVM_OP_BANK_object: {
                switch (*(cur_op++)) {
                    case MVM_OP_knowhow:
                        GET_REG(cur_op, 0).o = tc->instance->KnowHOW;
                        cur_op += 2;
                        break;
                    case MVM_OP_findmeth:
                        GET_REG(cur_op, 0).o = MVM_6model_find_method(tc,
                            GET_REG(cur_op, 2).o,
                            cu->strings[GET_UI16(cur_op, 4)]);
                        cur_op += 6;
                        break;
                    case MVM_OP_findmeth_s:
                        GET_REG(cur_op, 0).o = MVM_6model_find_method(tc,
                            GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_create: {
                        /* Ordering here matters. We write the object into the
                         * register before calling initialize. This is because
                         * if initialize allocates, obj may have moved after
                         * we called it. Note that type is never used after
                         * the initial allocate call also. This saves us having
                         * to put things on the temporary stack. */
                        MVMObject *type = GET_REG(cur_op, 2).o;
                        MVMObject *obj  = REPR(type)->allocate(tc, STABLE(type));
                        GET_REG(cur_op, 0).o = obj;
                        REPR(obj)->initialize(tc, STABLE(obj), obj, OBJECT_BODY(obj));
                        cur_op += 4;
                        break;
                    }
                    case MVM_OP_gethow:
                        GET_REG(cur_op, 0).o = STABLE(GET_REG(cur_op, 2).o)->HOW;
                        cur_op += 4;
                        break;
                    case MVM_OP_getwhat:
                        GET_REG(cur_op, 0).o = STABLE(GET_REG(cur_op, 2).o)->WHAT;
                        cur_op += 4;
                        break;
                    case MVM_OP_atkey_o: {
                        MVMObject *obj = GET_REG(cur_op, 2).o;
                        GET_REG(cur_op, 0).o = REPR(obj)->ass_funcs->at_key_boxed(tc,
                            STABLE(obj), obj, OBJECT_BODY(obj),
                            (MVMObject *)GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    }
                    case MVM_OP_bindkey_o: {
                        MVMObject *obj = GET_REG(cur_op, 0).o;
                        REPR(obj)->ass_funcs->bind_key_boxed(tc, STABLE(obj), obj,
                            OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s,
                            GET_REG(cur_op, 4).o);
                        cur_op += 6;
                        break;
                    }
                    case MVM_OP_existskey: {
                        MVMObject *obj = GET_REG(cur_op, 2).o;
                        GET_REG(cur_op, 0).i64 = REPR(obj)->ass_funcs->exists_key(tc,
                            STABLE(obj), obj, OBJECT_BODY(obj),
                            (MVMObject *)GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    }
                    case MVM_OP_deletekey: {
                        MVMObject *obj = GET_REG(cur_op, 0).o;
                        REPR(obj)->ass_funcs->delete_key(tc, STABLE(obj), obj,
                            OBJECT_BODY(obj), (MVMObject *)GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    }
                    case MVM_OP_elemskeyed: {
                        MVMObject *obj = GET_REG(cur_op, 2).o;
                        GET_REG(cur_op, 0).i64 = REPR(obj)->ass_funcs->elems(tc,
                            STABLE(obj), obj, OBJECT_BODY(obj));
                        cur_op += 4;
                        break;
                    }
                    case MVM_OP_eqaddr:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).o == GET_REG(cur_op, 4).o ? 1 : 0;
                        cur_op += 6;
                        break;
                    case MVM_OP_reprname:
                        GET_REG(cur_op, 0).s = REPR(GET_REG(cur_op, 2).o)->name;
                        cur_op += 4;
                        break;
                    case MVM_OP_isconcrete:
                        GET_REG(cur_op, 0).i64 = IS_CONCRETE(GET_REG(cur_op, 2).o) ? 1 : 0;
                        cur_op += 4;
                        break;
                    default: {
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_object, *(cur_op-1));
                    }
                    break;
                }
            }
            break;
            
            /* IO operations. */
            case MVM_OP_BANK_io: {
                switch (*(cur_op++)) {
                    case MVM_OP_copy_f:
                        MVM_file_copy(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_append_f:
                        MVM_file_copy(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_rename_f:
                        MVM_file_copy(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_delete_f:
                        MVM_file_delete(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_chmod_f:
                        MVM_file_chmod(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).i64);
                        cur_op += 4;
                        break;
                    case MVM_OP_exists_f:
                        GET_REG(cur_op, 0).i64 = MVM_file_exists(tc, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_mkdir:
                        MVM_dir_mkdir(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_rmdir:
                        MVM_dir_rmdir(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_open_dir:
                        GET_REG(cur_op, 0).o = MVM_dir_open(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).s);
                        cur_op += 6;
                        break;
                    case MVM_OP_read_dir:
                        GET_REG(cur_op, 0).s = MVM_dir_read(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_close_dir:
                        MVM_dir_close(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_open_fh:
                        GET_REG(cur_op, 0).o = MVM_file_open_fh(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64);
                        cur_op += 8;
                        break;
                    case MVM_OP_close_fh:
                        MVM_file_close_fh(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_read_fhs:
                        GET_REG(cur_op, 0).s = MVM_file_read_fhs(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_slurp:
                        GET_REG(cur_op, 0).s = MVM_file_slurp(tc, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_spew:
                        MVM_file_spew(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_write_fhs:
                        MVM_file_write_fhs(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).s,
                            GET_REG(cur_op, 4).i64, GET_REG(cur_op, 6).i64);
                        cur_op += 8;
                        break;
                    case MVM_OP_seek_fh:
                        MVM_file_seek(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64,
                            GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_lock_fh:
                        GET_REG(cur_op, 0).i64 = MVM_file_lock(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_unlock_fh:
                        MVM_file_unlock(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_flush_fh:
                        MVM_file_flush(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_sync_fh:
                        MVM_file_sync(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_pipe_fh:
                        MVM_file_pipe(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_trunc_fh:
                        MVM_file_truncate(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64);
                        cur_op += 4;
                        break;
                    case MVM_OP_eof_fh:
                        GET_REG(cur_op, 0).i64 = MVM_file_eof(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_getstdin:
                        GET_REG(cur_op, 0).o = MVM_file_get_stdin(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_getstdout:
                        GET_REG(cur_op, 0).o = MVM_file_get_stdout(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_getstderr:
                        GET_REG(cur_op, 0).o = MVM_file_get_stderr(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_connect_sk:
                        GET_REG(cur_op, 0).o = MVM_socket_connect(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64);
                        cur_op += 10;
                        break;
                    case MVM_OP_close_sk:
                        MVM_socket_close(tc, GET_REG(cur_op, 0).o);
                        cur_op += 2;
                        break;
                    case MVM_OP_bind_sk:
                        GET_REG(cur_op, 0).o = MVM_socket_bind(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64, GET_REG(cur_op, 8).i64);
                        cur_op += 10;
                        break;
                    case MVM_OP_listen_sk:
                        MVM_socket_listen(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).i64);
                        cur_op += 4;
                        break;
                    case MVM_OP_accept_sk:
                        GET_REG(cur_op, 0).o = MVM_socket_accept(tc, GET_REG(cur_op, 2).o);
                        cur_op += 4;
                        break;
                    case MVM_OP_send_sks:
                        MVM_socket_send_string(tc, GET_REG(cur_op, 0).o, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_recv_sks:
                        GET_REG(cur_op, 0).s = MVM_socket_receive_string(tc, GET_REG(cur_op, 2).o,
                            GET_REG(cur_op, 4).i64);
                        cur_op += 6;
                        break;
                    case MVM_OP_hostname:
                        GET_REG(cur_op, 0).s = MVM_socket_hostname(tc);
                        cur_op += 2;
                        break;
                    default: {
                        MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_object, *(cur_op-1));
                    }
                    break;
                }
            }
            break;
            
            /* Process-wide and thread operations. */
            case MVM_OP_BANK_processthread: {
                switch (*(cur_op++)) {
                    case MVM_OP_getenv:
                        GET_REG(cur_op, 0).s = MVM_proc_getenv(tc, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_setenv:
                        MVM_proc_setenv(tc, GET_REG(cur_op, 0).s, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_delenv:
                        MVM_proc_delenv(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_nametogid:
                        GET_REG(cur_op, 0).i64 = MVM_proc_nametogid(tc, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_gidtoname:
                        GET_REG(cur_op, 0).s = MVM_proc_gidtoname(tc, GET_REG(cur_op, 2).i64);
                        cur_op += 4;
                        break;
                    case MVM_OP_nametouid:
                        GET_REG(cur_op, 0).i64 = MVM_proc_nametouid(tc, GET_REG(cur_op, 2).s);
                        cur_op += 4;
                        break;
                    case MVM_OP_uidtoname:
                        GET_REG(cur_op, 0).s = MVM_proc_uidtoname(tc, GET_REG(cur_op, 2).i64);
                        cur_op += 4;
                        break;
                    case MVM_OP_getusername:
                        GET_REG(cur_op, 0).s = MVM_proc_getusername(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_getuid:
                        GET_REG(cur_op, 0).i64 = MVM_proc_getuid(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_getgid:
                        GET_REG(cur_op, 0).i64 = MVM_proc_getgid(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_gethomedir:
                        GET_REG(cur_op, 0).s = MVM_proc_gethomedir(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_chdir:
                        MVM_dir_chdir(tc, GET_REG(cur_op, 0).s);
                        cur_op += 2;
                        break;
                    case MVM_OP_rand_i:
                        GET_REG(cur_op, 0).i64 = MVM_proc_rand_i(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_rand_n:
                        GET_REG(cur_op, 0).n64 = MVM_proc_rand_n(tc);
                        cur_op += 2;
                        break;
                    case MVM_OP_time_i:
                        GET_REG(cur_op, 0).i64 = MVM_proc_time_i(tc);
                        cur_op += 2;
                        break;
                    default: {
                        MVM_panic(13, "Invalid opcode executed (corrupt bytecode stream?) bank %u opcode %u",
                                MVM_OP_BANK_object, *(cur_op-1));
                    }
                    break;
                }
            }
            break;
            
            /* Dispatch to bank function. */
            default:
            {
                MVM_panic(MVM_exitcode_invalidopcode, "Invalid opcode executed (corrupt bytecode stream?) bank %u", *(cur_op-1));
            }
            break;
        }
    }
}
