#include "moarvm.h"

/* Macros for getting things from the bytecode stream. */
/* GET_REG is defined differently here from interp.c */
#define GET_REG(pc, idx)    *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_I64(pc, idx)    *((MVMint64 *)(pc + idx))
#define GET_UI64(pc, idx)   *((MVMuint64 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))
#define GET_N64(pc, idx)    *((MVMnum64 *)(pc + idx))

static void cleanup_all(MVMThreadContext *tc, MVMuint8 *opstart_here, MVMuint8 *goto_here) {
    free(opstart_here);
    free(goto_here);
}

static void throw_past_end(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: truncated stream");
}


/* Validate that a static frame's bytecode is executable by the interpreter */
void MVM_validate_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {
    
    MVMuint32 bytecode_size = static_frame->bytecode_size;
    MVMuint8 *bytecode_start = static_frame->bytecode;
    MVMuint8 *bytecode_end = bytecode_start + bytecode_size;
    /* current position in the bytestream */
    MVMuint8 *cur_op = bytecode_start;
    /* positions in the bytestream that are starts of ops */
    MVMuint8 *opstart_here = malloc(bytecode_size);
    /* positions in the bytestream that are targets of a goto offset */
    MVMuint8 *goto_here = malloc(bytecode_size);
    MVMuint32 num_locals = static_frame->num_locals;
    MVMuint32 branch_target;
    MVMuint8 bank_num;
    MVMuint8 op_num;
    MVMOpInfo *op_info;
    MVMuint32 operand_size;
    MVMuint16 operand_target;
    int i;
    unsigned char op_rw;
    unsigned char op_type;
    unsigned char op_flags;
    
    memset(opstart_here, 0, bytecode_size);
    memset(goto_here, 0, bytecode_size);
    
    /* printf("bytecode_size %d cur_op %d bytecode_end %d difference %d", bytecode_size, (int)cur_op, (int)bytecode_end, (int)(bytecode_end - cur_op)); */
    while (cur_op < bytecode_end - 1) {
        opstart_here[cur_op - bytecode_start] = 1;
        bank_num = *(cur_op++);
        op_num = *(cur_op++);
        op_info = MVM_op_get_op((unsigned char)bank_num, (unsigned char)op_num);
        if (!op_info) {
            cleanup_all(tc, opstart_here, goto_here);
            MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: non-existent operation");
        }
        /*printf("validating op %s, (%d) bank %d", op_info->name, op_num, bank_num);*/
        for (i = 0; i < op_info->num_operands; i++) {
            op_flags = op_info->operands[i];
            op_rw   = op_flags & MVM_operand_rw_mask;
            op_type = op_flags & MVM_operand_type_mask;
            if (op_rw == MVM_operand_literal) {
                switch (op_type) {
                    case MVM_operand_int8:      operand_size = 1; break;
                    case MVM_operand_int16:     operand_size = 2; break;
                    case MVM_operand_int32:     operand_size = 4; break;
                    case MVM_operand_int64:     operand_size = 8; break;
                    case MVM_operand_num32:     operand_size = 4; break;
                    case MVM_operand_num64:     operand_size = 8; break;
                    case MVM_operand_callsite:
                        operand_size = 2;
                        if (cur_op + operand_size >= bytecode_end)
                            throw_past_end(tc);
                        operand_target = GET_UI16(cur_op, 0);
                        /* TODO XXX I don't know how to bounds check a literal callsite */
                        cur_op += operand_size;
                        operand_size = 0;
                        break;
                        
                    case MVM_operand_coderef:
                        /* TODO XXX I don't know how to bounds check a literal coderef */
                        operand_size = 2; break; /* reset to 0 */
                        
                    case MVM_operand_lex_outer: operand_size = 2; break;
                    
                    case MVM_operand_str:
                        /* TODO XXX I don't know how to bounds check a literal string */
                        operand_size = 2; break; /* reset to 0 */
                        
                    case MVM_operand_ins:
                        operand_size = 4;
                        if (cur_op + operand_size >= bytecode_end)
                            throw_past_end(tc);
                        branch_target = GET_UI32(cur_op, 0);
                        if (branch_target >= bytecode_size) {
                            cleanup_all(tc, opstart_here, goto_here);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: branch instruction offset out of range");
                        }
                        goto_here[branch_target] = 1;
                        cur_op += operand_size;
                        operand_size = 0; break;
                    
                    case MVM_operand_obj:
                    case MVM_operand_type_var:
                        MVM_exception_throw_adhoc(tc,
                            "Bytecode validation error: that operand type can't be a literal");
                        break;
                    default: {
                        MVM_exception_throw_adhoc(tc,
                            "Bytecode validation error: non-existent operand type");
                    }
                }
                if (cur_op + operand_size >= bytecode_end)
                    throw_past_end(tc);
            }
            else { /* register operand */
                operand_size = 2;
                if (cur_op + operand_size >= bytecode_end)
                    throw_past_end(tc);
                if (GET_REG(cur_op, 0) >= num_locals) {
                    cleanup_all(tc, opstart_here, goto_here);
                    MVM_exception_throw_adhoc(tc,
                        "Bytecode validation error: operand register index out of range");
                }
            }
            if (cur_op + operand_size >= bytecode_end)
                throw_past_end(tc);
            cur_op += operand_size;
        }
    }
    for (i = 0; i < bytecode_size; i++) {
        if (goto_here[i] && !opstart_here[i]) {
            cleanup_all(tc, opstart_here, goto_here);
            MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: branch to a non-op start position");
        }
    }
    cleanup_all(tc, opstart_here, goto_here);
}