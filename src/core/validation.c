#include "moar.h"

/* TODO: validate
 * - args of prepargs, getcode
 * - any cu->strings index (28 currently)
 * - cur_frame->args indexes
 */

/* Macros for getting things from the bytecode stream. */
/* GET_REG is defined differently here from interp.c */
#define GET_REG(pc, idx)    *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))

MVM_STATIC_INLINE MVMuint32 GET_UI32(const MVMuint8 *pc, MVMint32 idx) {
    MVMuint32 retval;
    memcpy(&retval, pc + idx, sizeof(retval));
    return retval;
}

#define MSG(val, msg) "Bytecode validation error at offset %" PRIu32 \
    ", instruction %" PRIu32 ":\n" msg, \
    (MVMuint32)((val)->cur_op - (val)->bc_start), (val)->cur_instr

MVM_STATIC_INLINE const char *operand_type_name(MVMuint8 op) {
    switch (op) {
        case MVM_operand_int8:     return "int8";
        case MVM_operand_int16:    return "int16";
        case MVM_operand_int32:    return "int32";
        case MVM_operand_int64:    return "int64";
        case MVM_operand_num32:    return "num32";
        case MVM_operand_num64:    return "num64";
        case MVM_operand_callsite: return "callsite";
        case MVM_operand_coderef:  return "coderef";
        case MVM_operand_str:      return "str";
        case MVM_operand_ins:      return "instruction";

        case MVM_operand_obj:      return "obj";
        case MVM_operand_type_var: return "typevar";
        default: return "?";
    }
}

enum {
    MARK_regular  = ' ',
    MARK_special  = '.',
    MARK_sequence = ':',
};

typedef struct {
    MVMThreadContext *tc;
    MVMCompUnit      *cu;
    MVMStaticFrame   *frame;
    MVMuint32         loc_count;
    MVMuint16        *loc_types;
    MVMuint32         bc_size;
    MVMuint8         *bc_start;
    MVMuint8         *bc_end;
    MVMuint8         *src_cur_op;
    MVMuint8         *src_bc_end;
    MVMuint8         *labels;
    MVMuint8         *cur_op;
    const MVMOpInfo  *cur_info;
    const char       *cur_mark;
    MVMuint32         cur_instr;
    MVMCallsite      *cur_call;
    MVMuint16         cur_arg;
    MVMint32          acceptable_max_arity;
    MVMint16          checkarity_seen;
    MVMCallsiteEntry  expected_named_arg;
    MVMuint16         remaining_args;
    MVMuint16         remaining_positionals;
    MVMuint32         remaining_jumplabels;
    MVMuint32         reg_type_var;
} Validator;


MVM_NO_RETURN static void fail(Validator *val, const char *msg, ...) MVM_FORMAT(printf, 2, 3) MVM_NO_RETURN_ATTRIBUTE;
static void fail(Validator *val, const char *msg, ...) {
    va_list args;

    va_start(args, msg);

    MVM_free(val->labels);
    MVM_exception_throw_adhoc_va(val->tc, msg, args);

    va_end(args);
}


static void fail_illegal_mark(Validator *val) {
    fail(val, MSG(val, "illegal op mark '%.2s'"), val->cur_mark);
}


static void ensure_bytes(Validator *val, MVMuint32 count) {
    if (val->src_cur_op + count > val->src_bc_end)
        fail(val, MSG(val, "truncated stream"));
#ifdef MVM_BIGENDIAN
    /* Endian swap equivalent of memcpy(val->cur_op, val->src_cur_op, count); */
    {
        MVMuint8 *d = val->cur_op + count;
        while (count--) {
            *--d = *val->src_cur_op++;
        }
    }
#else
    val->src_cur_op += count;
#endif
}


static void ensure_op(Validator *val, MVMuint16 opcode) {
    if (val->cur_info->opcode != opcode) {
        fail(val, MSG(val, "expected op %s but got %s"),
                MVM_op_get_op(opcode)->name, val->cur_info->name);
    }
}


static void ensure_no_remaining_jumplabels(Validator *val) {
    if (val->remaining_jumplabels != 0)
        fail(val, MSG(val, "%" PRIu32 " jumplist labels missing their goto ops"),
                val->remaining_jumplabels);
}


static void ensure_no_remaining_positionals(Validator *val) {
    if (val->remaining_positionals != 0)
        fail(val, MSG(val, "callsite expects %" PRIu16 " more positionals"),
                val->remaining_positionals);
}


static void ensure_no_remaining_args(Validator *val) {
    if (val->remaining_args != 0)
        fail(val, MSG(val, "callsite expects %" PRIu16 " more args"),
                val->remaining_args);
}


MVM_STATIC_INLINE const MVMOpInfo * get_info(Validator *val, MVMuint16 opcode) {
    const MVMOpInfo *info;

    if (opcode < MVM_OP_EXT_BASE) {
        info = MVM_op_get_op(opcode);
        if (!info)
            fail(val, MSG(val, "invalid opcode %u"), opcode);
    }
    else {
        MVMuint16 index = opcode - MVM_OP_EXT_BASE;
        MVMExtOpRecord *record;

        if (index >= val->cu->body.num_extops)
            fail(val, MSG(val,
                    "invalid extension opcode %u - should be less than %u"),
                    opcode, MVM_OP_EXT_BASE + val->cu->body.num_extops);

        record = &val->cu->body.extops[index];
        info = MVM_ext_resolve_extop_record(val->tc, record);
        if (!info)
            fail(val, MSG(val, "extension op '%s' not registered"),
                    MVM_string_utf8_encode_C_string(val->tc, record->name));
    }

    return info;
}


MVM_STATIC_INLINE void read_op(Validator *val) {
    MVMuint16  opcode;
    const MVMOpInfo *info;
    MVMuint32  pos;

    ensure_bytes(val, 2);

    opcode = *(MVMuint16 *)val->cur_op;
    info   = get_info(val, opcode);
    pos    = val->cur_op - val->bc_start;

#if 0
MVM_string_print(val->tc, val->cu->body.filename);
printf(" %u %s %.2s\n", val->cur_instr, info->name, info->mark);
#endif

    val->labels[pos] |= MVM_BC_op_boundary;
    val->cur_info     = info;
    val->cur_mark     = MVM_op_get_mark(opcode);
    val->cur_op      += 2;
    val->cur_instr   += 1;
}


static void unread_op(Validator *val) {
    val->src_cur_op -= 2;
    val->cur_op    -= 2;
    val->cur_instr -= 1;
}


static void validate_branch_targets(Validator *val) {
    MVMuint32 pos, instr;

    for (pos = 0, instr = (MVMuint32)-1; pos < val->bc_size; pos++) {
        MVMuint32 flag = val->labels[pos];

        if (flag & MVM_BC_op_boundary)
            instr++;

        if ((flag & MVM_BC_branch_target) && !(flag & MVM_BC_op_boundary))
            fail(val, MSG(val, "branch targets offset %" PRIu32
                " within instruction %" PRIu32), pos, instr);
    }
}


static void validate_final_return(Validator *val) {
    if (!val->bc_size || val->cur_mark[1] != 'r')
        fail(val, MSG(val, "missing final return instruction"));
}


static void validate_literal_operand(Validator *val, MVMuint32 flags) {
    MVMuint32 type = flags & MVM_operand_type_mask;
    MVMuint32 size;

    switch (type) {
        case MVM_operand_int8:     size = 1; break;
        case MVM_operand_int16:    size = 2; break;
        case MVM_operand_int32:    size = 4; break;
        case MVM_operand_int64:    size = 8; break;
        case MVM_operand_num32:    size = 4; break;
        case MVM_operand_num64:    size = 8; break;
        case MVM_operand_callsite: size = 2; break;
        case MVM_operand_coderef:  size = 2; break;
        case MVM_operand_str:      size = 4; break;
        case MVM_operand_ins:      size = 4; break;

        case MVM_operand_obj:
        case MVM_operand_type_var:
            fail(val, MSG(val, "operand type %"PRIu32" can't be a literal"), type);

        default:
            fail(val, MSG(val, "unknown operand type %"PRIu32), type);
    }

    ensure_bytes(val, size);

    switch (type) {
        case MVM_operand_callsite: {
            MVMuint16 index = GET_UI16(val->cur_op, 0);
            MVMuint32 count = val->cu->body.orig_callsites;
            if (index >= count)
                fail(val, MSG(val, "callsite index %" PRIu16
                        " out of range 0..%" PRIu32), index, count - 1);
            break;
        }

        case MVM_operand_coderef: {
            MVMuint16 index = GET_UI16(val->cur_op, 0);
            MVMuint32 count = val->cu->body.orig_frames;
            if (index >= count)
                fail(val, MSG(val, "coderef index %" PRIu16
                        " out of range 0..%" PRIu32), index, count - 1);
            break;
        }

        case MVM_operand_str: {
            MVMuint32 index = GET_UI32(val->cur_op, 0);
            MVMuint32 count = val->cu->body.orig_strings;
            if (index >= count)
                fail(val, MSG(val, "string index %" PRIu32
                        " out of range 0..%" PRIu32), index, count - 1);
            break;
        }

        case MVM_operand_ins: {
            MVMuint32 offset = GET_UI32(val->cur_op, 0);
            if (offset >= val->bc_size)
                fail(val, MSG(val, "branch instruction offset %" PRIu32
                        " out of range 0..%" PRIu32), offset, val->bc_size - 1);
            val->labels[offset] |= MVM_BC_branch_target;
        }
    }

    val->cur_op += size;
}


static void validate_reg_operand(Validator *val, MVMuint32 flags) {
    MVMuint32 operand_type = flags & MVM_operand_type_mask;
    MVMuint32 reg_type;
    MVMuint16 reg;

    ensure_bytes(val, 2);

    reg = GET_REG(val->cur_op, 0);
    if (reg >= val->loc_count)
        fail(val, MSG(val, "register operand index %" PRIu16
                " out of range 0..%" PRIu32), reg, val->loc_count - 1);

    reg_type = val->loc_types[reg] << 3;

    if (operand_type == MVM_operand_type_var) {
        if (!val->reg_type_var) {
            val->reg_type_var = reg_type;
            goto next_operand;
        }

        operand_type = val->reg_type_var;
    }

    if (reg_type != operand_type) {
        fail(val, MSG(val, "operand type %" PRIu32 " (%s) does not match register type %" PRIu32 " (%s) for op %s in frame %s"),
                operand_type, operand_type_name(operand_type), reg_type, operand_type_name(reg_type), val->cur_info->name, MVM_string_utf8_maybe_encode_C_string(val->tc, val->frame->body.name));
    }

next_operand:
    val->cur_op += 2;
}


static void validate_lex_operand(Validator *val, MVMuint32 flags) {
    MVMuint32 operand_type = flags & MVM_operand_type_mask;
    MVMuint16 lex_index, frame_index, i;
    MVMuint32 lex_count, lex_type;
    MVMStaticFrame *frame = val->frame;

    /* Two steps forward, two steps back to keep the error reporting happy,
       and to make the endian conversion within ensure_bytes correct.
       (Both are using val->cur_op, and want it to have different values.) */
    ensure_bytes(val, 2);
    lex_index   = GET_UI16(val->cur_op, 0);
    val->cur_op += 2;
    ensure_bytes(val, 2);
    val->cur_op -= 2;
    frame_index = GET_UI16(val->cur_op, 2);

    for (i = frame_index; i; i--) {
        frame = frame->body.outer;
        if (!frame)
            fail(val, MSG(val, "lexical operand requires %" PRIu16
                    " more enclosing scopes"), i);
    }

    if (!frame->body.fully_deserialized)
        MVM_bytecode_finish_frame(val->tc, frame->body.cu, frame, 0);

    lex_count = frame->body.num_lexicals;
    if (lex_index >= lex_count)
        fail(val, MSG(val, "lexical operand index %" PRIu16
                " out of range 0.. %" PRIu32), lex_index, lex_count - 1);

    lex_type = frame->body.lexical_types[lex_index] << 3;

    if (operand_type == MVM_operand_type_var) {
        if (!val->reg_type_var) {
            val->reg_type_var = lex_type;
            goto next_operand;
        }

        operand_type = val->reg_type_var;
    }

    if (lex_type != operand_type)
        fail(val, MSG(val, "operand type %"PRIu32" does not match lexical type %"PRIu32),
                operand_type, lex_type);

  next_operand:
    val->cur_op += 4;
}


static void validate_operand(Validator *val, MVMuint32 flags) {
    MVMuint32 rw = flags & MVM_operand_rw_mask;

    switch (rw) {
        case MVM_operand_literal:
            validate_literal_operand(val, flags);
            break;

        case MVM_operand_read_reg:
        case MVM_operand_write_reg:
            validate_reg_operand(val, flags);
            break;

        case MVM_operand_read_lex:
        case MVM_operand_write_lex:
            validate_lex_operand(val, flags);
            break;

        default:
            fail(val, MSG(val, "invalid instruction rw flag %"PRIu32), rw);
    }
}

static void validate_dispatch_args(Validator *val, MVMCallsite *cs) {
    MVMuint16 i;
    for (i = 0; i < cs->flag_count; i++) {
        MVMuint8 arg_type = cs->arg_flags[i] & MVM_CALLSITE_ARG_TYPE_MASK;
        switch (arg_type) {
            case MVM_CALLSITE_ARG_OBJ:
                validate_reg_operand(val, MVM_operand_obj);
                break;
            case MVM_CALLSITE_ARG_INT:
                validate_reg_operand(val, MVM_operand_int64);
                break;
            case MVM_CALLSITE_ARG_UINT:
                validate_reg_operand(val, MVM_operand_uint64);
                break;
            case MVM_CALLSITE_ARG_NUM:
                validate_reg_operand(val, MVM_operand_num64);
                break;
            case MVM_CALLSITE_ARG_STR:
                validate_reg_operand(val, MVM_operand_str);
                break;
            default:
                fail(val, MSG(val, "unrecognized callsite arg type %" PRIu8), arg_type);
        }
    }
}

static void validate_operands(Validator *val) {
    const MVMuint8 *operands = val->cur_info->operands;

    val->reg_type_var = 0;

    switch (val->cur_info->opcode) {
        case MVM_OP_jumplist: {
            MVMint64 count;

            validate_literal_operand(val, operands[0]);
            count = MVM_BC_get_I64(val->cur_op, -8);
            if (count < 0 || count > UINT32_MAX)
                fail(val, MSG(val, "illegal jumplist label count %" PRIi64),
                        count);

            validate_reg_operand(val, operands[1]);

            break;
        }
        case MVM_OP_checkarity: {
            validate_literal_operand(val, operands[0]);

            validate_literal_operand(val, operands[1]);
            val->acceptable_max_arity = GET_UI16(val->cur_op, -2);
            val->checkarity_seen = 1;

            break;
        }
        case MVM_OP_wval:
        case MVM_OP_wval_wide: {
            validate_reg_operand(val, operands[0]);
            validate_literal_operand(val, operands[1]);
            if (GET_UI16(val->cur_op, -2) >= val->cu->body.num_scs)
                fail(val, MSG(val, "out of range SC index %u"), GET_UI16(val->cur_op, -2));
            validate_literal_operand(val, operands[2]);
            break;
        }

        default: {
            int i;

            if (val->cur_mark[1] == 'p') {
                /* First of all, bail out if no checkarity was seen yet. */
                if (!val->checkarity_seen) {
                    fail(val, MSG(val, "param op without checkarity op seen."));
                }

                /* For the p-marked ops, which is a subset of param_* ops,
                 * we check the second argument against the value checkarity
                 * checked against. */
                for (i = 0; i < val->cur_info->num_operands; i++) {
                    validate_operand(val, val->cur_info->operands[i]);

                    /* This is the argument we want to check */
                    if (i == 1) {
                        MVMint16 value = GET_UI16(val->cur_op, -2);
                        if (value > val->acceptable_max_arity) {
                            fail(val, MSG(val, "tried to take arg number %d after checkarity with %d"), value, val->acceptable_max_arity);
                        }
                    }
                }
            }
            else {
                for (i = 0; i < val->cur_info->num_operands; i++)
                    validate_operand(val, val->cur_info->operands[i]);
                if (val->cur_mark[1] == 'd') {
                    /* Dispatch op needs its register args section validated. We
                     * already just validated the callsite is in range, so can
                     * fetch it unchecked here. */
                    MVMCallsite *cs = val->cu->body.callsites[GET_UI16(val->cur_op, -2)];
                    validate_dispatch_args(val, cs);
                }
            }
        }
    }
}


static void validate_sequence(Validator *val) {
    int seq_id = val->cur_mark[1];

    switch (seq_id) {
        case 'j': {
            ensure_op(val, MVM_OP_jumplist);
            validate_operands(val);
            val->remaining_jumplabels = (MVMuint32)MVM_BC_get_I64(val->cur_op, -10);
            break;
        }

        default:
            fail(val, MSG(val, "unknown instruction sequence '%c'"), seq_id);
    }

    while (val->cur_op < val->bc_end) {
        int type, id;

        read_op(val);
        type = val->cur_mark[0];
        id   = val->cur_mark[1];

        if (val->cur_info->specializable)
            val->frame->body.specializable = 1;

        switch (type) {
            case MARK_special:
                if (id == seq_id)
                    break;
                /* FALLTHROUGH */

            case MARK_regular:
            case MARK_sequence:
                unread_op(val);
                goto terminate_seq;

            default:
                fail_illegal_mark(val);
        }

        switch (seq_id) {
            case 'j':
                ensure_op(val, MVM_OP_goto);
                validate_operands(val);

                val->remaining_jumplabels--;
                if (val->remaining_jumplabels == 0)
                    goto terminate_seq;
                break;
        }
    }

terminate_seq:
    switch (seq_id) {
        case 'j':
            ensure_no_remaining_jumplabels(val);
            break;
    }
}


/* Validate that a static frame's bytecode is executable by the interpreter. */
void MVM_validate_static_frame(MVMThreadContext *tc,
        MVMStaticFrame *static_frame) {
    MVMStaticFrameBody *fb = &static_frame->body;
    Validator val[1];

    val->tc        = tc;
    val->cu        = fb->cu;
    val->frame     = static_frame;
    val->loc_count = fb->num_locals;
    val->loc_types = fb->local_types;
    val->bc_size   = fb->bytecode_size;
    val->src_cur_op = fb->bytecode;
    val->src_bc_end = fb->bytecode + fb->bytecode_size;
    val->labels    = MVM_calloc(1, fb->bytecode_size);
    val->cur_info  = NULL;
    val->cur_mark  = NULL;
    val->cur_instr = 0;
    val->cur_call  = NULL;
    val->cur_arg   = 0;

    val->acceptable_max_arity  = 0;
    val->checkarity_seen       = 0;

    val->expected_named_arg    = 0;
    val->remaining_positionals = 0;
    val->remaining_jumplabels  = 0;
    val->reg_type_var          = 0;

#ifdef MVM_BIGENDIAN
    assert(fb->bytecode == fb->orig_bytecode);
    val->bc_start = MVM_malloc(fb->bytecode_size);
    memset(val->bc_start, 0xDB, fb->bytecode_size);
    fb->bytecode = val->bc_start;
#else
    val->bc_start = fb->bytecode;
#endif
    val->bc_end = val->bc_start + fb->bytecode_size;
    val->cur_op = val->bc_start;

    while (val->cur_op < val->bc_end) {
        read_op(val);
        if (val->cur_mark && val->cur_mark[0] == 's')
            fail(val, MSG(val, "Illegal appearance of spesh op"));
        if (val->cur_info->specializable)
            fb->specializable = 1;

        switch (val->cur_mark[0]) {
            case MARK_regular:
            case MARK_special:
                validate_operands(val);
                break;

            case MARK_sequence:
                validate_sequence(val);
                break;

            default:
                fail_illegal_mark(val);
        }
    }

    validate_branch_targets(val);
    validate_final_return(val);

    /* Validation successful. Clear up instruction offsets. */
    MVM_free(val->labels);

    /* Mark frame validated. */
    static_frame->body.validated = 1;
}
