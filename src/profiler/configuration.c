#include "moar.h"

typedef struct {
    MVMConfigurationProgram *confprog;
    MVMuint8 *bytecode_root;
    MVMuint8 *bc_pointer;

    MVMuint32  jumptarget_count;
    MVMuint32 *jumptarget_queue;

    MVMuint16  register_count;
    MVMuint16  register_alloc;
    MVMuint8  *register_types;

    const MVMOpInfo *cur_op;
} validatorstate;

static MVMuint8 operand_size(MVMThreadContext *tc, MVMuint8 operand) {
    MVMuint32 type = operand & MVM_operand_type_mask;
    MVMuint32 kind = operand & MVM_operand_rw_mask;
    MVMuint32 size;

    if (kind == MVM_operand_literal) {
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
                MVM_exception_throw_adhoc(tc, "TODO: object or type var operands not allowed for literal");
                /*fail(val, MSG(val, "operand type %"PRIu32" can't be a literal"), type);*/

            default:
                MVM_exception_throw_adhoc(tc, "TODO: unknown operand type");
                /*fail(val, MSG(val, "unknown operand type %"PRIu32), type);*/
        }
    }
    else if (kind == MVM_operand_read_reg || kind == MVM_operand_write_reg) {
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
            case MVM_operand_type_var: size = 2; break;
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "TODO: lexical operands NYI");
    }

    return size;
}

static void validate_reg_operand(MVMThreadContext *tc, validatorstate *state, MVMuint8 operand) {
    MVMuint8 size = operand_size(tc, operand);

    MVMuint16 reg_num = *((MVMuint16 *)state->bc_pointer);

    if (reg_num > state->register_alloc) {
        MVMuint16 old_register_alloc = state->register_alloc;
        state->register_alloc = (reg_num | 0x7) + 1;
        state->register_types = MVM_recalloc(state->register_types, old_register_alloc, state->register_alloc);
    }
    if (reg_num > state->register_count) {
        state->register_count = reg_num + 1;
        state->register_types[reg_num] = (operand << 1) | 1;
    }
    else {
        if (state->register_types[reg_num] != ((operand << 1) | 1)) {
            MVM_exception_throw_adhoc(tc, "Invalid operand write; type was already seen as %d before, now supposed to be %d", state->register_types[reg_num] >> 1, operand);
        }
    }

    state->bc_pointer += size;
}

static void validate_literal_operand(MVMThreadContext *tc, validatorstate *state, MVMuint8 operand) {
    MVMuint8 size = operand_size(tc, operand);
    state->bc_pointer += size;
}

static void validate_operand(MVMThreadContext *tc, validatorstate *state, MVMuint8 operand) {
    MVMuint8 rw = operand & MVM_operand_rw_mask;

    switch (rw) {
        case MVM_operand_literal:
            validate_literal_operand(tc, state, operand);
            break;

        case MVM_operand_read_reg:
        case MVM_operand_write_reg:
            validate_reg_operand(tc, state, operand);
            break;

        default:
            MVM_exception_throw_adhoc(tc, "TODO: invalid instruction rw flag");
            /*fail(val, MSG(val, "invalid instruction rw flag %"PRIu32), rw);*/
    }
}

static void validate_operands(MVMThreadContext *tc, validatorstate *state) {
    const MVMuint8 *operands = state->cur_op->operands;
    int i = 0;

    for (i = 0; i < state->cur_op->num_operands; i++) {
        validate_operand(tc, state, state->cur_op->operands[i]);
    }
}
static void validate_op(MVMThreadContext *tc, validatorstate *state) {
    MVMuint16 opcode = *((MVMuint16 *)state->bc_pointer);
    const MVMOpInfo *info;
    if (!MVM_op_is_allowed_in_confprog(opcode)) {
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d (%s) at position 0x%x",
                opcode, MVM_op_get_op(opcode)->name, state->bytecode_root - state->bc_pointer);
    }
    info = MVM_op_get_op(opcode);
    if (!info)
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d  at position 0x%x",
                opcode, state->bytecode_root - state->bc_pointer);

    state->cur_op = info;

    state->bc_pointer += 2;
    validate_operands(tc, state);
}

MVMuint8 MVM_confprog_validate(MVMThreadContext *tc, MVMConfigurationProgram *prog) {
    validatorstate state;

    state.confprog = prog;
    state.bytecode_root = prog->bytecode;
    state.bc_pointer = prog->bytecode;

    state.jumptarget_count = 0;
    state.jumptarget_queue = NULL;

    state.register_count = 3;
    state.register_alloc = 4;
    state.register_types = MVM_calloc(4, 1);

    state.register_types[0] = 1;
    state.register_types[1] = 1;
    state.register_types[2] = 1;

    state.cur_op = NULL;

    while (state.bc_pointer < state.bytecode_root + prog->bytecode_length) {
        validate_op(tc, &state);
    }

    prog->reg_types = state.register_types;
    prog->reg_count = state.register_count;
}

#define CHECK_CONC(obj, type, purpose) do { if (MVM_UNLIKELY(MVM_is_null(tc, obj) == 1 || IS_CONCRETE((MVMObject *)(obj)) == 0 || REPR((MVMObject *)(obj))->ID != MVM_REPR_ID_ ## type)) { error_concreteness(tc, (MVMObject *)(obj), MVM_REPR_ID_ ## type, purpose); } } while (0)
static void error_concreteness(MVMThreadContext *tc, MVMObject *object, MVMuint16 reprid, char *purpose) {
    if (!object)
        MVM_exception_throw_adhoc(tc, "installconfprog requires a %s for %s (got null instead)",
            MVM_repr_get_by_id(tc, reprid)->name, purpose);
    if (REPR(object)-> ID != reprid)
        MVM_exception_throw_adhoc(tc, "installconfprog requires a %s for %s (got a %s of type %s instead)",
            MVM_repr_get_by_id(tc, reprid)->name, purpose, REPR(object)->name, MVM_6model_get_debug_name(tc, object));
    else
        MVM_exception_throw_adhoc(tc, "installconfprog requires a concrete %s for %s (got a type objecd %s (a %s) instead)",
            MVM_repr_get_by_id(tc, reprid)->name, purpose, MVM_6model_get_debug_name(tc, object), REPR(object)->name);
}

void MVM_confprog_install(MVMThreadContext *tc, MVMObject *bytecode, MVMObject *string_array, MVMObject *entrypoints) {
    MVMuint64 bytecode_size;
    MVMuint8 *array_contents;
    MVMConfigurationProgram *confprog;

    CHECK_CONC(bytecode, VMArray, "the bytecode");
    CHECK_CONC(string_array, VMArray, "the string heap");
    CHECK_CONC(entrypoints, VMArray, "the entrypoints list");

    {
        MVMObject *arr = bytecode;
        MVMArrayREPRData *reprdata = (MVMArrayREPRData *)STABLE(arr)->REPR_data;

        if (reprdata->slot_type != MVM_ARRAY_U8) {
            MVM_exception_throw_adhoc(tc, "installconfprog requires the bytecode array to be a native array of uint8 (got a %s)",
                    MVM_6model_get_debug_name(tc, bytecode));
        }

        bytecode_size = MVM_repr_elems(tc, bytecode);

        if (bytecode_size % 2 == 1) {
            MVM_exception_throw_adhoc(tc, "installconfprog expected bytecode array to be a multiple of 2 bytes big (got a %d)",
                    bytecode_size);
        }

        if (bytecode_size > 4096) {
            MVM_exception_throw_adhoc(tc, "confprog too big. maximum 4096, this one has %d", bytecode_size);
        }

        array_contents = ((MVMArray *)bytecode)->body.slots.u8;
    }

    {
        MVMObject *arr = string_array;
        MVMArrayREPRData *reprdata = (MVMArrayREPRData *)STABLE(arr)->REPR_data;

        if (reprdata->slot_type != MVM_ARRAY_STR) {
            MVM_exception_throw_adhoc(tc, "installconfprog requires the string heap array to be a native array of strings (got a %s)",
                    MVM_6model_get_debug_name(tc, bytecode));
        }
    }

    {
        MVMObject *arr = entrypoints;
        MVMArrayREPRData *reprdata = (MVMArrayREPRData *)STABLE(arr)->REPR_data;

        if (reprdata->slot_type != MVM_ARRAY_I64) {
            MVM_exception_throw_adhoc(tc, "installconfprog requires the entrypoints array to be a native array of 64-bit integers (got a %s)",
                    MVM_6model_get_debug_name(tc, bytecode));
        }
    }

    confprog = MVM_calloc(sizeof(MVMConfigurationProgram), 1);

    confprog->bytecode = MVM_malloc(bytecode_size);
    memcpy(confprog->bytecode, array_contents, bytecode_size);

    MVM_confprog_validate(tc, confprog);

    confprog->string_heap = string_array;

    MVM_confprog_run(tc, confprog, tc->instance->VMNull, 0);
}

/* Stolen from interp.c */
#define OP(name) case MVM_OP_ ## name
#define NEXT runloop
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]

#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))

void MVM_confprog_run(MVMThreadContext *tc, MVMConfigurationProgram *prog, MVMObject *subject, MVMuint8 entrypoint) {
    MVMuint8 *cur_op;

    MVMRegister *reg_base = MVM_calloc(prog->reg_count, sizeof(MVMRegister));

    cur_op = prog->bytecode;

    runloop: {
        MVMuint16 ins = *((MVMuint16 *)cur_op);
        cur_op += 2;
        switch (ins) {
            OP(no_op):
                cur_op += 2;
                fprintf(stderr, "noop\n");
                goto NEXT;
            OP(const_i64):
                GET_REG(cur_op, 0).i64 = MVM_BC_get_I64(cur_op, 2);
                fprintf(stderr, "const_i64 %d\n", MVM_BC_get_I64(cur_op, 2));
                cur_op += 10;
                goto NEXT;
            OP(const_s):
                /*GET_REG(cur_op, 0).s = MVM_cu_string(tc, cu, GET_UI32(cur_op, 2));*/
                MVM_string_say(tc, MVM_repr_at_pos_s(tc, prog->string_heap, GET_UI32(cur_op, 2)));
                fprintf(stderr, "^- contents of const_s\n");
                cur_op += 6;
                goto NEXT;
            OP(set):
                GET_REG(cur_op, 0) = GET_REG(cur_op, 2);
                fprintf(stderr, "set\n");
                cur_op += 4;
                goto NEXT;
            OP(say):
                MVM_string_say(tc, GET_REG(cur_op, 0).s);
                fprintf(stderr, "say\n");
                cur_op += 2;
                goto NEXT;
            OP(getattr_o):
                fprintf(stderr, "getattr_o\n");
                cur_op += 12;
                goto NEXT;
            default:
                fprintf(stderr, "operation %s (%d, 0x%x) NYI\n", MVM_op_get_op(ins)->name, ins, ins);
        }
    }
}

#define add_collectable(tc, worklist, snapshot, col, desc) \
    do { \
        if (worklist) { \
            MVM_gc_worklist_add(tc, worklist, &(col)); \
        } \
        else { \
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, snapshot, \
                (MVMCollectable *)col, desc); \
        } \
    } while (0)

void MVM_confprog_mark(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot) {
    MVMConfigurationProgram *confprog = tc->instance->confprog;
    add_collectable(tc, worklist, snapshot, confprog->string_heap,
        "Configuration Program String Heap");
}
