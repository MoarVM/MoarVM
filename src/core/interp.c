#include "moarvm.h"

/* This is the interpreter run loop. We have one of these per thread. */
void MVM_interp_run(MVMThreadContext *tc) {
    /* Points to the current opcode. */
    MVMuint8 *cur_op; /* XXX set... */
    
    /* Points to the base of the current register set for the frame we
     * are presently in. */
    MVMRegister *reg_base; /* XXX set... */
    
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
