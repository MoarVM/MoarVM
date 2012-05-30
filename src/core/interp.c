#include "moarvm.h"

/* Macros for getting things from the bytecode stream. */
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]
#define GET_I64(pc, idx)    *((MVMint64 *)(pc + idx))

/* This is the interpreter run loop. We have one of these per thread. */
void MVM_interp_run(MVMThreadContext *tc, MVMFrame *initial_frame) {
    /* Points to the current opcode. */
    MVMuint8 *cur_op = initial_frame->static_info->bytecode;
    
    /* Points to the base of the current register set for the frame we
     * are presently in. */
    MVMRegister *reg_base = initial_frame->work;
    
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
                    case MVM_OP_return:
                        return;
                    case MVM_OP_const_i64:
                        GET_REG(cur_op, 0).i64 = GET_I64(cur_op, 2);
                        cur_op += 10;
                        break;
                    case MVM_OP_add_i:
                        GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 + GET_REG(cur_op, 4).i64;
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
