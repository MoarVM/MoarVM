#include "moar.h"

enum {
    StructSel_Nothing,
    StructSel_Root,
    StructSel_MVMStaticFrame,
    StructSel_MVMFrame,
};

enum {
    FieldSel_staticframe,
};

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
    const MVMOpInfo *prev_op;

    MVMuint8 *prev_op_bc;
    
    MVMuint8 selected_struct_source;
} validatorstate;

typedef union CPRegister {
    MVMObject         *o;
    MVMString         *s;
    void              *any;
    MVMint8            i8;
    MVMuint8           u8;
    MVMint16           i16;
    MVMuint16          u16;
    MVMint32           i32;
    MVMuint32          u32;
    MVMint64           i64;
    MVMuint64          u64;
    MVMnum32           n32;
    MVMnum64           n64;
} CPRegister;

/* special "fake" registers for the program that have very specific
 * use cases for some select ops */
#define REGISTER_STRUCT_SELECT 0
#define REGISTER_STRUCT_ACCUMULATOR 1
#define REGISTER_FEATURE_TOGGLE 2

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
            case MVM_operand_int8:     size = 2; break;
            case MVM_operand_int16:    size = 2; break;
            case MVM_operand_int32:    size = 2; break;
            case MVM_operand_int64:    size = 2; break;
            case MVM_operand_num32:    size = 2; break;
            case MVM_operand_num64:    size = 2; break;
            case MVM_operand_callsite: size = 2; break;
            case MVM_operand_coderef:  size = 2; break;
            case MVM_operand_str:      size = 2; break;
            case MVM_operand_ins:      size = 2; break;

            case MVM_operand_obj:
            case MVM_operand_type_var: size = 2; break;
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "TODO: lexical operands NYI");
    }

    return size;
}

static void validate_reg_operand(MVMThreadContext *tc, validatorstate *state, MVMuint16 operand_idx, MVMuint8 operand) {
    MVMuint8 size = operand_size(tc, operand);

    MVMuint16 reg_num = *((MVMuint16 *)state->bc_pointer);

    if (reg_num > state->register_alloc) {
        MVMuint16 old_register_alloc = state->register_alloc;
        state->register_alloc = (reg_num | 0x7) + 1;
        state->register_types = MVM_recalloc(state->register_types, old_register_alloc, state->register_alloc);
    }

    fprintf(stderr, "encountered register operand of op %s for reg_num %d, current register count %d\n", state->cur_op->name, reg_num, state->register_count);

    if (reg_num > state->register_count) {
        state->register_count = reg_num + 1;
        state->register_types[reg_num] = (operand << 1) | 1;
    }
    else {
        if (state->register_types[reg_num] != ((operand << 1) | 1)) {
            fprintf(stderr, "Invalid operand write; type of register %d was already seen as %d before, now supposed to be %d\n", reg_num, state->register_types[reg_num] >> 1, operand);
            /*MVM_exception_throw_adhoc(tc, "Invalid operand write; type was already seen as %d before, now supposed to be %d", state->register_types[reg_num] >> 1, operand);*/
        }
    }

    state->bc_pointer += size;
}

static void validate_literal_operand(MVMThreadContext *tc, validatorstate *state, MVMuint16 operand_idx, MVMuint8 operand) {
    MVMuint8 size = operand_size(tc, operand);
    state->bc_pointer += size;
}

static void validate_operand(MVMThreadContext *tc, validatorstate *state, MVMuint16 operand_idx, MVMuint8 operand) {
    MVMuint8 rw = operand & MVM_operand_rw_mask;

    switch (rw) {
        case MVM_operand_literal:
            validate_literal_operand(tc, state, operand_idx, operand);
            break;

        case MVM_operand_read_reg:
        case MVM_operand_write_reg:
            validate_reg_operand(tc, state, operand_idx, operand);
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
        validate_operand(tc, state, i, state->cur_op->operands[i]);
    }
}
static void validate_op(MVMThreadContext *tc, validatorstate *state) {
    MVMuint16 opcode = *((MVMuint16 *)state->bc_pointer);
    const MVMOpInfo *info;
    MVMuint8 *prev_op_bc_ptr = state->bc_pointer;

    fprintf(stderr, "validate op at %x\n", state->bc_pointer - state->bytecode_root);

    if (!MVM_op_is_allowed_in_confprog(opcode)) {
        /*MVMuint16 op;*/
        /*for (op = 0; op < 916; op++) {*/
            /*if (MVM_op_is_allowed_in_confprog(op)) {*/
                /*fprintf(stderr, "op %s (%d) is allowed\n", MVM_op_get_op(op)->name, op);*/
            /*}*/
        /*}*/
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d (%s) at position 0x%x",
                opcode, MVM_op_get_op(opcode)->name, state->bc_pointer - state->bytecode_root);
    }
    info = MVM_op_get_op(opcode);
    if (!info)
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d  at position 0x%x",
                opcode, state->bc_pointer - state->bytecode_root);

    state->prev_op = state->cur_op;
    state->cur_op = info;

    state->bc_pointer += 2;

    /* const_s into the STRUCT_SELECT fake register influences how getattr
     * behaves (a terrible hack, really), so we check what it does.
     *
     * Also, in this case it will turn into a const_i64_16 instead. To fill
     * up empty space we put a no-op in front
     */
    if (opcode == MVM_OP_const_s) {
        MVMuint16 reg_num = *((MVMuint16 *)state->bc_pointer);
        MVMuint32 string_idx;
        validate_operand(tc, state, 0, state->cur_op->operands[0]);
        string_idx = *((MVMuint32 *)state->bc_pointer);
        validate_operand(tc, state, 1, state->cur_op->operands[1]);

        fprintf(stderr, "after validating const_s, %x, before %x, diff %x\n", state->bc_pointer, prev_op_bc_ptr, state->bc_pointer - prev_op_bc_ptr);

        if (reg_num == REGISTER_STRUCT_SELECT) {
            MVMObject *string_at_position = MVM_repr_at_pos_s(tc, state->confprog->string_heap, string_idx);
            MVMuint64 string_length;

            if (MVM_is_null(tc, string_at_position)) {
                MVM_exception_throw_adhoc(tc, "Invalid string put into STRUCT_SELECT register: index %d\n",
                        string_idx);
            }

            string_length = MVM_string_graphs(tc, string_at_position);
            switch (string_length) {
                case 0:
                    state->selected_struct_source = StructSel_Root;
                    fprintf(stderr, "next getattr will operate on 'root' namespace\n");
                    break;

                case 8:
                    /* TODO string comparison */
                    state->selected_struct_source = StructSel_MVMFrame;
                    fprintf(stderr, "next getattr will operate on an MVMFrame\n");
                    break;

                case 14:
                    /* TODO string comparison */
                    state->selected_struct_source = StructSel_MVMStaticFrame;
                    fprintf(stderr, "next getattr will operate on an MVMStaticFrame\n");
                    break;

                default:
                    MVM_exception_throw_adhoc(tc, "STRUCT_SELECT string NYI or something");
            }

            /* Now do a rewrite of const_s into const_i64_16 and noop */
            *((MVMuint16 *)(prev_op_bc_ptr)) = MVM_OP_no_op;
            *((MVMuint16 *)(prev_op_bc_ptr + 2)) = MVM_OP_const_i64_16;
            *((MVMuint16 *)(prev_op_bc_ptr + 4)) = REGISTER_STRUCT_SELECT;
            *((MVMuint16 *)(prev_op_bc_ptr + 6)) = state->selected_struct_source;

            state->bc_pointer = prev_op_bc_ptr;

            return;
        }
    }
    else if (opcode == MVM_OP_getattr_o) {
        MVMuint8 selected_struct_source = state->selected_struct_source; 
        MVMuint16 target_reg_num;
        MVMuint16 source_reg_num;
        MVMuint32 string_idx;
        MVMuint16 hint;
        MVMuint16 *hintptr;

        target_reg_num = *((MVMuint16 *)state->bc_pointer);
        validate_operand(tc, state, 0, state->cur_op->operands[0]);

        source_reg_num = *((MVMuint16 *)state->bc_pointer);
        validate_operand(tc, state, 1, state->cur_op->operands[1]);

        validate_operand(tc, state, 2, state->cur_op->operands[2]);

        string_idx = *((MVMuint32 *)state->bc_pointer);
        validate_operand(tc, state, 3, state->cur_op->operands[3]);

        hintptr = (MVMuint16 *)state->bc_pointer;
        hint = *hintptr;
        validate_operand(tc, state, 4, state->cur_op->operands[4]);

        fprintf(stderr, "currently on %d struct source, string index is %d\n", selected_struct_source, string_idx);

        if (source_reg_num == REGISTER_STRUCT_ACCUMULATOR) {
            MVMObject *string_at_position = MVM_repr_at_pos_s(tc, state->confprog->string_heap, string_idx);
            MVMuint64 string_length = MVM_string_graphs(tc, string_at_position);
            /* Special "fake" getattr */
            if (selected_struct_source == StructSel_Root) {
                fprintf(stderr, "hint pointer will select a root struct entry\n");
                /* staticframe; TODO string comparison */
                if (string_length == 11) { /* staticframe */
                    *hintptr = FieldSel_staticframe;
                }
            }
            else if (selected_struct_source == StructSel_MVMStaticFrame) {
                if (string_length == 2) { /* cu */
                    *hintptr = offsetof(MVMStaticFrame, body.cu);
                }
                else if (string_length == 4) { /* name */
                    *hintptr = offsetof(MVMStaticFrame, body.name);
                }
                else if (string_length == 5) { /* cuuid, outer */
                    if (MVM_string_ord_at(tc, string_at_position, 0) == 'c') {
                        *hintptr = offsetof(MVMStaticFrame, body.cuuid);
                    }
                    else if (MVM_string_ord_at(tc, string_at_position, 0) == 'o') {
                        *hintptr = offsetof(MVMStaticFrame, body.outer);
                    }
                    else {
                        MVM_exception_throw_adhoc(tc, "STRUCT_SELECT string NYI or something");
                    }
                }
                else {
                    MVM_exception_throw_adhoc(tc, "STRUCT_SELECT is MVMStaticFrame, no field with length %d (string heap index %d) implemented", string_length, string_idx);
                }
            }

            fprintf(stderr, "set hint pointer value to %d\n", *hintptr);
        }
        else {
            /* Totally regular getattr call */
        }

        state->selected_struct_source = StructSel_Nothing;
    }
    else {
        validate_operands(tc, state);
    }
    state->prev_op_bc = prev_op_bc_ptr;
}

MVMuint8 MVM_confprog_validate(MVMThreadContext *tc, MVMConfigurationProgram *prog) {
    validatorstate state;

    state.confprog = prog;
    state.bytecode_root = prog->bytecode;
    state.bc_pointer = prog->bytecode;
    state.prev_op_bc = prog->bytecode;

    state.jumptarget_count = 0;
    state.jumptarget_queue = NULL;

    state.register_count = 3;
    state.register_alloc = 4;
    state.register_types = MVM_calloc(4, 1);

    state.register_types[0] = 117;
    state.register_types[1] = 21;
    state.register_types[2] = 117;

    state.cur_op = NULL;
    state.prev_op = NULL;

    state.selected_struct_source = 0;

    fprintf(stderr, "going to validate program\n");
    while (state.bc_pointer < state.bytecode_root + prog->bytecode_length) {
        fprintf(stderr, "\nvalidating bytecode at position 0x%x\n", state.bc_pointer - state.bytecode_root);
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

    confprog->bytecode_length = bytecode_size;

    confprog->string_heap = string_array;

    MVM_confprog_validate(tc, confprog);

    MVM_confprog_run(tc, confprog, tc->instance->VMNull, 0);
}

/* Stolen from interp.c */
#define OP(name) case MVM_OP_ ## name
#define NEXT runloop
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]

#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))

void MVM_confprog_run(MVMThreadContext *tc, MVMConfigurationProgram *prog, MVMObject *subject, MVMuint8 entrypoint) {
    MVMuint8 *cur_op;

    CPRegister *reg_base = MVM_calloc(prog->reg_count + 1, sizeof(CPRegister));

    cur_op = prog->bytecode;

    runloop: {
        MVMuint16 ins = *((MVMuint16 *)cur_op);
        cur_op += 2;
        switch (ins) {
            OP(no_op):
                fprintf(stderr, "noop\n");
                goto NEXT;
            OP(const_i64):
                GET_REG(cur_op, 0).i64 = MVM_BC_get_I64(cur_op, 2);
                fprintf(stderr, "const_i64 %d\n", MVM_BC_get_I64(cur_op, 2));
                cur_op += 10;
                goto NEXT;
            OP(const_i64_16):
                GET_REG(cur_op, 0).i64 = GET_I16(cur_op, 2);
                fprintf(stderr, "const_i64_16 %d into %d\n", GET_I16(cur_op, 2), GET_UI16(cur_op, 0));
                cur_op += 4;
                goto NEXT;
            OP(const_s):
                GET_REG(cur_op, 0).s = MVM_repr_at_pos_s(tc, prog->string_heap, GET_UI32(cur_op, 2));
                fprintf(stderr, "const_s into reg %d\n", GET_UI16(cur_op, 0));
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
                fprintf(stderr, "struct select: %x\n", reg_base[REGISTER_STRUCT_SELECT].i64);
                if (reg_base[REGISTER_STRUCT_SELECT].i64 == StructSel_Nothing) {
                    fprintf(stderr, "getattr_o into %d\n", GET_UI16(cur_op, 0));
                }
                else if (reg_base[REGISTER_STRUCT_SELECT].i64 == StructSel_Root) {
                    MVMuint16 field_select = GET_UI16(cur_op, 10);
                    if (field_select == FieldSel_staticframe) {
                        reg_base[REGISTER_STRUCT_ACCUMULATOR].any = tc->cur_frame->static_info;
                        fprintf(stderr, "get a static frame into the struct accumulator: %x\n", tc->cur_frame->static_info);
                    }
                    else {
                        fprintf(stderr, "NYI case of getattr_o on root struct hit\n");
                    }
                }
                else {
                    fprintf(stderr, "getting the struct accumulator's field at offset 0x%x into register %d\n",
                            GET_UI16(cur_op, 10), GET_UI16(cur_op, 0));
                    fprintf(stderr, "register %d contents before: 0x%x\n", GET_UI16(cur_op, 0), reg_base[GET_UI16(cur_op, 0)].any);
                    fprintf(stderr, "register %d contents before: 0x%x\n", REGISTER_STRUCT_ACCUMULATOR, reg_base[REGISTER_STRUCT_ACCUMULATOR].any);
                    reg_base[GET_UI16(cur_op, 0)].any =
                        *((void **)(((char *)reg_base[REGISTER_STRUCT_ACCUMULATOR].any) + GET_UI16(cur_op, 10)));
                    fprintf(stderr, "register %d contents now: 0x%x\n", GET_UI16(cur_op, 0), reg_base[GET_UI16(cur_op, 0)].any);
                    /*(char *)(&reg_base[REGISTER_STRUCT_ACCUMULATOR]) = (char *)*/
                }
                cur_op += 12;
                goto NEXT;
            OP(eq_s):
                fprintf(stderr, "eq_s with %d and %d\n", GET_UI16(cur_op, 2), GET_UI16(cur_op, 4));
                GET_REG(cur_op, 0).i64 = MVM_string_equal(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(ne_s):
                GET_REG(cur_op, 0).i64 = (MVMint64)(MVM_string_equal(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s)? 0 : 1);
                cur_op += 6;
                goto NEXT;
            OP(gt_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) == 1;
                cur_op += 6;
                goto NEXT;
            OP(ge_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) >= 0;
                cur_op += 6;
                goto NEXT;
            OP(lt_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) == -1;
                cur_op += 6;
                goto NEXT;
            OP(le_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s) <= 0;
                cur_op += 6;
                goto NEXT;
            OP(cmp_s):
                GET_REG(cur_op, 0).i64 = MVM_string_compare(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s);
                cur_op += 6;
                goto NEXT;
            OP(smrt_strify): {
                /* Increment PC before calling coercer, as it may make
                 * a method call to get the result. */
                MVMObject   *obj = GET_REG(cur_op, 2).o;
                MVMRegister *res = &GET_REG(cur_op, 0);
                cur_op += 4;
                MVM_coerce_smart_stringify(tc, obj, res);
                goto NEXT;
            }
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
