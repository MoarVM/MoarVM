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

enum {
    MVM_val_branch_target = 1,
    MVM_val_op_boundary   = 2
};

static void cleanup_all(MVMThreadContext *tc, MVMuint8 *labels) {
    free(labels);
}

static void throw_past_end(MVMThreadContext *tc, MVMuint8 *labels) {
    cleanup_all(tc, labels);
    MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: truncated stream");
}

/* TODO: validate args of prepargs, getcode, and any cu->strings index (28 currently).
 * Also disallow anything that can branch from running between a prepargs and invoke.
 * Also any cur_frame->args indexes. Also whether the argument assign op is the right
 * type for the arg flag corresponding to the argsite. */

/* Validate that a static frame's bytecode is executable by the interpreter */
void MVM_validate_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame) {

    MVMCompUnit *cu = static_frame->cu;
    MVMuint32 bytecode_size = static_frame->bytecode_size;
    MVMuint8 *bytecode_start = static_frame->bytecode;
    MVMuint8 *bytecode_end = bytecode_start + bytecode_size;
    /* current position in the bytestream */
    MVMuint8 *cur_op = bytecode_start;
    /* positions in the bytestream that are starts of ops and goto targets */
    MVMuint8 *labels = malloc(bytecode_size);
    MVMuint32 num_locals = static_frame->num_locals;
    MVMuint32 branch_target;
    MVMuint8 bank_num;
    MVMuint8 op_num;
    MVMOpInfo *op_info;
    MVMuint32 operand_size;
    MVMuint16 operand_target;
    MVMuint32 instruction = 0;
    MVMuint32 i;
    unsigned char op_rw;
    unsigned char op_type;
    unsigned char op_flags;
    MVMuint32 operand_type_var;
    MVMint64 num_jumplist_labels = 0;

    memset(labels, 0, bytecode_size);

    /* printf("bytecode_size %d cur_op %d bytecode_end %d difference %d", bytecode_size, (int)cur_op, (int)bytecode_end, (int)(bytecode_end - cur_op)); */
    while (cur_op < bytecode_end - 1) {
        labels[cur_op - bytecode_start] |= MVM_val_op_boundary;
        bank_num = *(cur_op++);
        op_num = *(cur_op++);
        operand_type_var = 0;
        op_info = MVM_op_get_op((unsigned char)bank_num, (unsigned char)op_num);
        if (!op_info) {
            cleanup_all(tc, labels);
            MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: non-existent operation bank %u op %u",
                bank_num, op_num);
        }
        if (num_jumplist_labels != 0 && num_jumplist_labels-- != 0
                && (bank_num != MVM_OP_BANK_primitives || op_num != MVM_OP_goto)) {
            cleanup_all(tc, labels);
            MVM_exception_throw_adhoc(tc,
                "jumplist op must be followed by an additional %d goto ops", num_jumplist_labels + 1);
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
                    case MVM_operand_int64:
                        operand_size = 8;
                        if (bank_num == MVM_OP_BANK_primitives && op_num == MVM_OP_jumplist) {
                            if (cur_op + operand_size > bytecode_end)
                                throw_past_end(tc, labels);
                            num_jumplist_labels = GET_I64(cur_op, 0);
                            if (num_jumplist_labels < 0 || num_jumplist_labels > 4294967295u) {
                                cleanup_all(tc, labels);
                                MVM_exception_throw_adhoc(tc,
                                    "num_jumplist_labels %d out of range", num_jumplist_labels);
                            }
                        }
                        break;
                    case MVM_operand_num32:     operand_size = 4; break;
                    case MVM_operand_num64:     operand_size = 8; break;
                    case MVM_operand_callsite:
                        operand_size = 2;
                        if (cur_op + operand_size > bytecode_end)
                            throw_past_end(tc, labels);
                        operand_target = GET_UI16(cur_op, 0);
                        if (operand_target >= cu->num_callsites) {
                            cleanup_all(tc, labels);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: callsites index (%u) out of range; frame has %u callsites",
                                operand_target, cu->num_callsites);
                        }
                        break;

                    case MVM_operand_coderef:
                        operand_size = 2;
                        if (cur_op + operand_size > bytecode_end)
                            throw_past_end(tc, labels);
                        operand_target = GET_UI16(cur_op, 0);
                        if (operand_target >= cu->num_frames) {
                            cleanup_all(tc, labels);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: coderef index (%u) out of range; frame has %u coderefs",
                                operand_target, cu->num_frames);
                        }
                        break; /* reset to 0 */

                    case MVM_operand_str:
                        operand_size = 2;
                        if (cur_op + operand_size > bytecode_end)
                            throw_past_end(tc, labels);
                        operand_target = GET_UI16(cur_op, 0);
                        if (operand_target >= cu->num_strings) {
                            cleanup_all(tc, labels);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: strings index (%u) out of range (0-%u)",
                                operand_target, cu->num_strings - 1);
                        }
                        break;

                    case MVM_operand_ins:
                        operand_size = 4;
                        if (cur_op + operand_size > bytecode_end)
                            throw_past_end(tc, labels);
                        branch_target = GET_UI32(cur_op, 0);
                        if (branch_target >= bytecode_size) {
                            cleanup_all(tc, labels);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: branch instruction offset (%u) out of range; frame has %u bytes",
                                branch_target, bytecode_size);
                        }
                        labels[branch_target] |= MVM_val_branch_target;
                        break;

                    case MVM_operand_obj:
                    case MVM_operand_type_var:
                        cleanup_all(tc, labels);
                        MVM_exception_throw_adhoc(tc,
                            "Bytecode validation error: that operand type (%u) can't be a literal",
                            (MVMuint8)op_type);
                        break;
                    default: {
                        cleanup_all(tc, labels);
                        MVM_exception_throw_adhoc(tc,
                            "Bytecode validation error: non-existent operand type (%u)",
                            (MVMuint8)op_type);
                    }
                }
                if (cur_op + operand_size > bytecode_end)
                    throw_past_end(tc, labels);
            }
            else if (op_rw == MVM_operand_read_reg || op_rw == MVM_operand_write_reg) {
                /* register operand */
                operand_size = 2;
                if (cur_op + operand_size > bytecode_end)
                    throw_past_end(tc, labels);
                if (GET_REG(cur_op, 0) >= num_locals) {
                    cleanup_all(tc, labels);
                    MVM_exception_throw_adhoc(tc,
                        "Bytecode validation error: operand register index (%u) out of range; frame has %u locals; at byte %u",
                        GET_REG(cur_op, 0), num_locals, cur_op - bytecode_start);
                }
                if (op_type == MVM_operand_type_var) {
                    if (operand_type_var) {
                        /* XXX assume only one type variable */
                        if ((static_frame->local_types[GET_REG(cur_op, 0)] << 3) != operand_type_var) {
                            cleanup_all(tc, labels);
                            MVM_exception_throw_adhoc(tc,
                                "Bytecode validation error: inconsistent operand types %d and %d to op '%s' with a type variable, at instruction %d",
                                    static_frame->local_types[GET_REG(cur_op, 0)], operand_type_var >> 3, op_info->name, instruction);
                        }
                    }
                    else {
                        operand_type_var = (static_frame->local_types[GET_REG(cur_op, 0)] << 3);
                    }
                }
                else if ((static_frame->local_types[GET_REG(cur_op, 0)] << 3) != op_type) {
                    cleanup_all(tc, labels);
                    MVM_exception_throw_adhoc(tc,
                        "Bytecode validation error: instruction operand type does not match register type");
                }
            }
            else if (op_rw == MVM_operand_read_lex || op_rw == MVM_operand_write_lex) {
                /* lexical operand */
                MVMuint16 idx, frames, i;
                MVMStaticFrame *applicable_frame = static_frame;

                /* Check we've enough bytecode left to read the operands, and
                 * do so. */
                operand_size = 4;
                if (cur_op + operand_size > bytecode_end)
                    throw_past_end(tc, labels);
                idx = GET_UI16(cur_op, 0);
                frames = GET_UI16(cur_op, 2);

                /* Locate the applicable static frame. */
                i = frames;
                while (i > 0) {
                    if (applicable_frame->outer) {
                        applicable_frame = applicable_frame->outer;
                    }
                    else {
                        cleanup_all(tc, labels);
                        MVM_exception_throw_adhoc(tc,
                            "Bytecode validation error: operand lexical outer frame count (%u) at byte %u",
                            frames, cur_op - bytecode_start);
                    }
                    i--;
                }

                /* Ensure that the lexical index is in range. */
                if (idx >= applicable_frame->num_lexicals) {
                    cleanup_all(tc, labels);
                    MVM_exception_throw_adhoc(tc,
                        "Bytecode validation error: operand lexical index (%u) out of range; frame has %u lexicals; at byte %u",
                        idx, applicable_frame->num_lexicals, cur_op - bytecode_start);
                }

                /* XXX Type checks. */
            }
            else {
                cleanup_all(tc, labels);
                    MVM_exception_throw_adhoc(tc,
                        "Bytecode validation error: invalid instruction rw mask");
            }
            cur_op += operand_size;
        }
        instruction++;
    }
    if (num_jumplist_labels) {
        cleanup_all(tc, labels);
        MVM_exception_throw_adhoc(tc,
            "jumplist op must be followed by an additional %d goto ops", num_jumplist_labels);
    }

    /* check that all the branches and gotos have valid op boundary destinations */
    for (i = 0; i < bytecode_size; i++) {
        if (labels[i] & MVM_val_branch_target && !(labels[i] & MVM_val_op_boundary)) {
            cleanup_all(tc, labels);
            MVM_exception_throw_adhoc(tc,
                "Bytecode validation error: branch to a non-op start position at instruction %u", i);
        }
    }
    cleanup_all(tc, labels);
    /* check that the last op is a return of some sort so we don't run off the */
    /* XXX TODO maybe also allow tailcalls of some sort, but currently compiler.c
     * adds the trailing return anyway, so... */
    if (!bytecode_size || bank_num != MVM_OP_BANK_primitives
            || (   op_num != MVM_OP_return
                && op_num != MVM_OP_return_i
                && op_num != MVM_OP_return_n
                && op_num != MVM_OP_return_s
                && op_num != MVM_OP_return_o)) {
        MVM_exception_throw_adhoc(tc,
            "Bytecode validation error: missing final return instruction");
    }
}
