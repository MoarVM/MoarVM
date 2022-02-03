#include "moar.h"

/* This file contains the "confprog" validator and interpreter.
 *
 * It is there to allow users to give specific behaviour to a few internal
 * systems of moarvm.
 *
 * At the start, it's for configuring for what static frames (or frames)
 * the profiler should start profiling (if it isn't already profiling), and
 * for configuring when the heap snapshot profiler should take a snapshot
 *
 * There is a Perl 6 module that offers a confprog compiler named
 * App::MoarVM::ConfprogCompiler, and nqp's src/vm/moar/HLL/Backend.nqp
 * has a corresponding loader. The nqp and perl6 commandline programs
 * recognize a --confprog flag.
 */

#define CONFPROG_UNUSED_ENTRYPOINT 1

#define OUTPUT_LOTS_OF_JUNK 0

#if OUTPUT_LOTS_OF_JUNK
#define junkprint fprintf
#else
#define junkprint(fh, str, ...) do { } while (0)
#endif

#define CONFPROG_DEBUG_LEVEL_BARE  1
#define CONFPROG_DEBUG_LEVEL_TRACE 2

// see src/profiler/log.c for the define for debug level 4, which is PROFILER_RESULTS

static void debugprint(MVMuint8 active, MVMThreadContext *tc, const char *str, ...) {
    va_list args;
    va_start(args, str);

    if (active) {
        fprintf(stderr, "%p: ", tc);
        vfprintf(stderr, str, args);
        fprintf(stderr, "\n");
    }

    va_end(args);
}

#define DEBUG_LVL(level) ((debug_level) & CONFPROG_DEBUG_LEVEL_ ## level)

enum {
    StructSel_Nothing,
    StructSel_Root,
    StructSel_MVMStaticFrame,
    StructSel_MVMFrame,
    StructSel_MVMCompUnit,
};

enum {
    FieldSel_staticframe,
    FieldSel_frame,
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
            default:
                MVM_exception_throw_adhoc(tc, "TODO: unknown operand type");
                /*fail(val, MSG(val, "unknown operand type %"PRIu32), type);*/
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

    junkprint(stderr, "encountered register operand of op %s for reg_num %d, current register count %d\n", state->cur_op->name, reg_num, state->register_count);

    if (reg_num > state->register_count) {
        state->register_count = reg_num + 1;
        state->register_types[reg_num] = (operand << 1) | 1;
    }
    else {
        if (state->register_types[reg_num] != ((operand << 1) | 1)) {
            junkprint(stderr, "Invalid operand write; type of register %d was already seen as %d before, now supposed to be %d\n", reg_num, state->register_types[reg_num] >> 1, operand);
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
        validate_operand(tc, state, i, operands[i]);
    }
}
static void validate_op(MVMThreadContext *tc, validatorstate *state) {
    MVMuint16 opcode = *((MVMuint16 *)state->bc_pointer);
    const MVMOpInfo *info;
    MVMuint8 *prev_op_bc_ptr = state->bc_pointer;

    junkprint(stderr, "validate op at %lx\n", state->bc_pointer - state->bytecode_root);

    if (!MVM_op_is_allowed_in_confprog(opcode)) {
        /*MVMuint16 op;*/
        /*for (op = 0; op < 916; op++) {*/
            /*if (MVM_op_is_allowed_in_confprog(op)) {*/
                /*junkprint(stderr, "op %s (%d) is allowed\n", MVM_op_get_op(op)->name, op);*/
            /*}*/
        /*}*/
        /* Sigh, so, pointer differences are ptrdiff_t, and (at least on some 32
         * bit platforms) that's `int`,
         * (even though on those platforms sizeof(int) == sizeof(long))
         * and so compilers warn about format string mismatch.
         * And sigh again, it's not until C11 that we get %tx to specify the
         * size as that of ptrdiff_t, so we can't rely on on that here.
         * So cast to unsigned long. */
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d (%s) at position 0x%lx",
                opcode, MVM_op_get_op(opcode)->name, (unsigned long) (state->bc_pointer - state->bytecode_root));
    }
    info = MVM_op_get_op(opcode);
    if (!info)
        MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d  at position 0x%lx",
                opcode, (unsigned long) (state->bc_pointer - state->bytecode_root));

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

        junkprint(stderr, "after validating const_s, %x, before %x, diff %x\n", state->bc_pointer, prev_op_bc_ptr, state->bc_pointer - prev_op_bc_ptr);

        if (reg_num == REGISTER_STRUCT_SELECT) {
            MVMObject *string_at_position = (MVMObject *)MVM_repr_at_pos_s(tc, state->confprog->string_heap, string_idx);
            MVMuint64 string_length;

            if (MVM_is_null(tc, string_at_position)) {
                MVM_exception_throw_adhoc(tc, "Invalid string put into STRUCT_SELECT register: index %d\n",
                        string_idx);
            }

            string_length = MVM_string_graphs(tc, (MVMString *)string_at_position);
            switch (string_length) {
                case 0:
                    state->selected_struct_source = StructSel_Root;
                    junkprint(stderr, "next getattr will operate on 'root' namespace\n");
                    break;

                case 8:
                    /* TODO string comparison */
                    state->selected_struct_source = StructSel_MVMFrame;
                    junkprint(stderr, "next getattr will operate on an MVMFrame\n");
                    break;

                case 11:
                    /* TODO string comparison */
                    state->selected_struct_source = StructSel_MVMCompUnit;
                    junkprint(stderr, "next getattr will operate on an MVMCompUnit\n");
                    break;

                case 14:
                    /* TODO string comparison */
                    state->selected_struct_source = StructSel_MVMStaticFrame;
                    junkprint(stderr, "next getattr will operate on an MVMStaticFrame\n");
                    break;

                default:
                    MVM_exception_throw_adhoc(tc, "STRUCT_SELECT string length %"PRIu64" (index %d) NYI or something", string_length, string_idx);
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
        MVMuint16 source_reg_num;
        MVMuint32 string_idx;
        MVMuint16 *hintptr;

        validate_operand(tc, state, 0, state->cur_op->operands[0]);

        source_reg_num = *((MVMuint16 *)state->bc_pointer);
        validate_operand(tc, state, 1, state->cur_op->operands[1]);

        validate_operand(tc, state, 2, state->cur_op->operands[2]);

        string_idx = *((MVMuint32 *)state->bc_pointer);
        validate_operand(tc, state, 3, state->cur_op->operands[3]);

        hintptr = (MVMuint16 *)state->bc_pointer;
        validate_operand(tc, state, 4, state->cur_op->operands[4]);

        junkprint(stderr, "currently on %d struct source, string index is %d\n", selected_struct_source, string_idx);

        if (source_reg_num == REGISTER_STRUCT_ACCUMULATOR) {
            MVMObject *string_at_position = (MVMObject *)MVM_repr_at_pos_s(tc, state->confprog->string_heap, string_idx);
            MVMuint64 string_length = MVM_string_graphs(tc, (MVMString *)string_at_position);
            /* Special "fake" getattr */
            if (selected_struct_source == StructSel_Root) {
                junkprint(stderr, "hint pointer will select a root struct entry\n");
                /* staticframe; TODO string comparison */
                if (string_length == 11) { /* staticframe */
                    *hintptr = FieldSel_staticframe;
                }
                else if (string_length == 5) { /* frame */
                    *hintptr = FieldSel_frame;
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
                    if (MVM_string_ord_at(tc, (MVMString *)string_at_position, 0) == 'c') {
                        *hintptr = offsetof(MVMStaticFrame, body.cuuid);
                    }
                    else if (MVM_string_ord_at(tc, (MVMString *)string_at_position, 0) == 'o') {
                        *hintptr = offsetof(MVMStaticFrame, body.outer);
                    }
                    else {
                        MVM_exception_throw_adhoc(tc, "STRUCT_SELECT string NYI or something");
                    }
                }
                else {
                    MVM_exception_throw_adhoc(tc, "STRUCT_SELECT is MVMStaticFrame, no field with length %"PRIu64" (string heap index %d) implemented", string_length, string_idx);
                }
            }
            else if (selected_struct_source == StructSel_MVMCompUnit) {
                if (string_length == 8) { /* filename or hll_name */
                    if (MVM_string_ord_at(tc, (MVMString *)string_at_position, 0) == 'f') {
                        *hintptr = offsetof(MVMCompUnit, body.filename);
                    }
                    else if (MVM_string_ord_at(tc, (MVMString *)string_at_position, 0) == 'h') {
                        *hintptr = offsetof(MVMCompUnit, body.hll_name);
                    }
                    else {
                        MVM_exception_throw_adhoc(tc, "STRUCT_SELECT is MVMCompUnit, no field with length %"PRIu64" (string heap index %d) implemented", string_length, string_idx);
                    }
                }
            }

            junkprint(stderr, "set hint pointer value to %d\n", *hintptr);
        }
        else {
            /* Totally regular getattr call */
        }

        state->selected_struct_source = StructSel_Nothing;
    }
    else if (opcode == MVM_OP_getcodelocation) {
        MVMuint16 new_opcode;
        const MVMOpInfo *new_info;

        validate_operand(tc, state, 0, state->cur_op->operands[0]);
        validate_operand(tc, state, 1, state->cur_op->operands[1]);

        new_opcode = *((MVMuint16 *)state->bc_pointer);
        state->bc_pointer += 2;

        new_info = MVM_op_get_op(new_opcode);
        if (!new_info)
            MVM_exception_throw_adhoc(tc, "Invalid opcode detected in confprog: %d  at position 0x%lx",
                    opcode, (unsigned long) (state->bc_pointer - state->bytecode_root));

        state->prev_op = state->cur_op;
        state->cur_op = new_info;

        if (new_opcode == MVM_OP_unbox_s) {

            validate_operand(tc, state, 0, state->cur_op->operands[0]);
            validate_operand(tc, state, 1, state->cur_op->operands[1]);
        }
        else if (new_opcode == MVM_OP_unbox_i) {

            validate_operand(tc, state, 0, state->cur_op->operands[0]);
            validate_operand(tc, state, 1, state->cur_op->operands[1]);
        }
        else {
            MVM_exception_throw_adhoc(tc, "Confprog: invalid opcode %s followed getcodelocation; only unbox_s and unbox_i are allowed.", MVM_op_get_op(new_opcode)->name);
        }
    }
    else {
        validate_operands(tc, state);
    }
    state->prev_op_bc = prev_op_bc_ptr;
}

void MVM_confprog_validate(MVMThreadContext *tc, MVMConfigurationProgram *prog) {
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

    junkprint(stderr, "going to validate program\n");
    while (state.bc_pointer < state.bytecode_root + prog->bytecode_length) {
        junkprint(stderr, "\nvalidating bytecode at position 0x%x\n", state.bc_pointer - state.bytecode_root);
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

static MVMint16 stats_position_for_value(MVMThreadContext *tc, MVMuint8 entrypoint, MVMuint64 return_value) {
    switch (entrypoint) {
        case MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC:
            if (return_value == MVM_CONFPROG_SF_RESULT_TO_BE_DETERMINED
                || return_value == MVM_CONFPROG_SF_RESULT_NEVER
                || return_value == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_NO
                || return_value == MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES
                || return_value == MVM_CONFPROG_SF_RESULT_ALWAYS
            ) {
                return return_value;
            }
            if (tc)
                MVM_exception_throw_adhoc(tc, "Can't get stats for out-of-bounds entrypoint number %d", entrypoint);
            return -1;
        case MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC:
            if (return_value == 0 || return_value == 1)
                return MVM_CONFPROG_SF_RESULT_ALWAYS + 1 + return_value;
            MVM_exception_throw_adhoc(tc, "Can't get stats for out-of-bounds value %"PRIu64" for dynamic profiler entrypoint", return_value);
            return -1;
        case MVM_PROGRAM_ENTRYPOINT_HEAPSNAPSHOT:
            if (return_value <= 2)
                return MVM_CONFPROG_SF_RESULT_ALWAYS + 1 + 1 + 1 + return_value;
            MVM_exception_throw_adhoc(tc, "Can't get stats for out-of-bounds value %"PRIu64" for heapsnapshot entrypoint", return_value);
            return -1;
        default:
            if (tc)
                MVM_exception_throw_adhoc(tc, "Can't get stats for out-of-bounds entrypoint number %d", entrypoint);
            return -1;
    }
}

void MVM_confprog_install(MVMThreadContext *tc, MVMObject *bytecode, MVMObject *string_array, MVMObject *entrypoints) {
    MVMuint64 bytecode_size;
    MVMuint8 *array_contents;
    MVMConfigurationProgram *confprog;
    MVMint16 entrypoint_array[MVM_PROGRAM_ENTRYPOINT_COUNT];

    MVMuint8 debug_level = getenv("MVM_CONFPROG_DEBUG") != NULL ? atoi(getenv("MVM_CONFPROG_DEBUG")) : 0;

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

        junkprint(stderr, "got a bytecode array with %d (%x) entries\n", bytecode_size, bytecode_size);

        if (bytecode_size % 2 == 1) {
            MVM_exception_throw_adhoc(tc, "installconfprog expected bytecode array to be a multiple of 2 bytes big (got a %"PRIu64")",
                    bytecode_size);
        }

        if (bytecode_size > 4096) {
            MVM_exception_throw_adhoc(tc, "confprog too big. maximum 4096, this one has %"PRIu64"", bytecode_size);
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
        MVMuint64 index;
        MVMuint64 count;

        if (reprdata->slot_type != MVM_ARRAY_I64) {
            MVM_exception_throw_adhoc(tc, "installconfprog requires the entrypoints array to be a native array of 64-bit integers (got a %s)",
                    MVM_6model_get_debug_name(tc, bytecode));
        }

        count = MVM_repr_elems(tc, arr);

        /* Initialize with sentry value */
        for (index = 0; index < MVM_PROGRAM_ENTRYPOINT_COUNT; index++) {
            entrypoint_array[index] = CONFPROG_UNUSED_ENTRYPOINT;
        }

        junkprint(stderr, "copying over %d entrypoints\n", count);

        for (index = 0; index < count && index < MVM_PROGRAM_ENTRYPOINT_COUNT; index++) {
            entrypoint_array[index] = MVM_repr_at_pos_i(tc, arr, index);
            junkprint(stderr, "  - %d == %d\n", index, entrypoint_array[index]);
        }
    }

    confprog = MVM_calloc(1, sizeof(MVMConfigurationProgram));

    confprog->debugging_level = debug_level;

    junkprint(stderr, "copying %d (%x) bytecode entries\n", bytecode_size, bytecode_size);
    confprog->bytecode = MVM_malloc(bytecode_size);
    memcpy(confprog->bytecode, array_contents, bytecode_size);

    memcpy(confprog->entrypoints, entrypoint_array, sizeof(entrypoint_array));

    confprog->bytecode_length = bytecode_size;

    confprog->string_heap = string_array;

    MVM_confprog_validate(tc, confprog);

    tc->instance->confprog = confprog;
}

MVMuint8 MVM_confprog_has_entrypoint(MVMThreadContext *tc, MVMuint8 entrypoint) {
    return tc->instance->confprog && entrypoint < MVM_PROGRAM_ENTRYPOINT_COUNT && tc->instance->confprog->entrypoints[entrypoint] != CONFPROG_UNUSED_ENTRYPOINT;
}

/* Stolen from interp.c */
#define OP(name) case MVM_OP_ ## name
#define NEXT runloop
#define GET_REG(pc, idx)    reg_base[*((MVMuint16 *)(pc + idx))]

#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))

#define STRING_OR_EMPTY(input) ((input) == 0 ? tc->instance->str_consts.empty : (input))

MVMint64 MVM_confprog_run(MVMThreadContext *tc, void *subject, MVMuint8 entrypoint, MVMint64 initial_feature_value) {
    MVMConfigurationProgram *prog = tc->instance->confprog;
    MVMuint8 *cur_op;
    MVMuint8 *last_op;
    MVMint64 result;

    MVMint16 stats_slot;

    MVMuint8 *bytecode_start;

    MVMuint8 debug_level = prog->debugging_level;

    CPRegister *reg_base = MVM_calloc(prog->reg_count + 1, sizeof(CPRegister));

    reg_base[REGISTER_FEATURE_TOGGLE].i64 = initial_feature_value;

    bytecode_start = prog->bytecode;
    cur_op = bytecode_start + prog->entrypoints[entrypoint];
    last_op = bytecode_start + prog->bytecode_length;

    MVM_incr(&prog->invoke_counts[entrypoint]);

    debugprint(DEBUG_LVL(BARE), tc, "  running confprog for entrypoint %d (at position 0x%x) (this entrypoint run for the %dth time)\n", entrypoint, prog->entrypoints[entrypoint], MVM_load(&prog->invoke_counts[entrypoint]));
    /*junkprint(stderr, "confprog is 0x%x (%d) bytes big", last_op - bytecode_start, last_op - bytecode_start);*/


    runloop: {
        MVMuint16 ins;

        if (cur_op >= last_op) {
            junkprint(stderr, "end of program: %p > %p\n", cur_op, last_op);
            goto finish_main_loop;
        }
        ins = *((MVMuint16 *)cur_op);
        cur_op += 2;

        junkprint(stderr, "evaluating a %s at position %x (%d)\n", MVM_op_get_op(ins)->name, cur_op - 2 - bytecode_start, cur_op - 2 - bytecode_start);

        switch (ins) {
            OP(no_op):
                debugprint(DEBUG_LVL(TRACE), tc, "noop");
                goto NEXT;
            OP(const_i64):
                GET_REG(cur_op, 0).i64 = MVM_BC_get_I64(cur_op, 2);
                debugprint(DEBUG_LVL(TRACE), tc, "const_i64 %ld into %d", MVM_BC_get_I64(cur_op, 2), GET_UI16(cur_op, 0));
                cur_op += 10;
                goto NEXT;
            OP(const_n64):
                GET_REG(cur_op, 0).n64 = MVM_BC_get_N64(cur_op, 2);
                debugprint(DEBUG_LVL(TRACE), tc, "const_n64 %f into %d", MVM_BC_get_N64(cur_op, 2), GET_UI16(cur_op, 0));
                cur_op += 10;
                goto NEXT;
            OP(const_i64_16):
                GET_REG(cur_op, 0).i64 = GET_I16(cur_op, 2);
                debugprint(DEBUG_LVL(TRACE), tc, "const_i64_16 %d into %d", GET_I16(cur_op, 2), GET_UI16(cur_op, 0));
                cur_op += 4;
                goto NEXT;
            OP(const_s):
                GET_REG(cur_op, 0).s = MVM_repr_at_pos_s(tc, prog->string_heap, GET_UI32(cur_op, 2));
                debugprint(DEBUG_LVL(TRACE), tc, "const_s into reg %d", GET_UI16(cur_op, 0));
                cur_op += 6;
                goto NEXT;
            OP(set):
                GET_REG(cur_op, 0) = GET_REG(cur_op, 2);
                debugprint(DEBUG_LVL(TRACE), tc, "set %d <- %d", GET_UI16(cur_op, 0), GET_UI16(cur_op, 2));
                cur_op += 4;
                goto NEXT;
            OP(say): {
                MVMString *s = GET_REG(cur_op, 0).s;
                debugprint(DEBUG_LVL(TRACE), tc, "say %d", GET_UI16(cur_op, 0));
                if (s && IS_CONCRETE(s))
                    MVM_string_say(tc, s);
                cur_op += 2;
                goto NEXT;
            }
            OP(getattr_o):
                debugprint(DEBUG_LVL(TRACE), tc, "struct select: %lx", (unsigned long)reg_base[REGISTER_STRUCT_SELECT].i64);
                if (reg_base[REGISTER_STRUCT_SELECT].i64 == StructSel_Nothing) {
                    debugprint(DEBUG_LVL(TRACE), tc, "getattr_o into %d", GET_UI16(cur_op, 0));
                }
                else if (reg_base[REGISTER_STRUCT_SELECT].i64 == StructSel_Root) {
                    MVMuint16 field_select = GET_UI16(cur_op, 10);
                    if (field_select == FieldSel_staticframe) {
                        reg_base[REGISTER_STRUCT_ACCUMULATOR].any = tc->cur_frame->static_info;
                        debugprint(DEBUG_LVL(TRACE), tc, "get a static frame into the struct accumulator: %p", (void *)tc->cur_frame->static_info);
                    }
                    else if (field_select == FieldSel_frame) {
                        reg_base[REGISTER_STRUCT_ACCUMULATOR].any = tc->cur_frame;
                        debugprint(DEBUG_LVL(TRACE), tc, "get a frame into the struct accumulator: %p", (void *)tc->cur_frame);
                    }
                    else {
                        fprintf(stderr, "NYI case of getattr_o on root struct hit\n");
                        goto finish_main_loop;
                    }
                }
                else {
                    debugprint(DEBUG_LVL(TRACE), tc, "getting the struct accumulator's field at offset 0x%x into register %d",
                            GET_UI16(cur_op, 10), GET_UI16(cur_op, 0));
                    debugprint(DEBUG_LVL(TRACE), tc, "register %d contents before: 0x%p", GET_UI16(cur_op, 0), reg_base[GET_UI16(cur_op, 0)].any);
                    debugprint(DEBUG_LVL(TRACE), tc, "register %d contents before: 0x%p", REGISTER_STRUCT_ACCUMULATOR, reg_base[REGISTER_STRUCT_ACCUMULATOR].any);
                    reg_base[GET_UI16(cur_op, 0)].any =
                        *((void **)(((char *)reg_base[REGISTER_STRUCT_ACCUMULATOR].any) + GET_UI16(cur_op, 10)));
                    debugprint(DEBUG_LVL(TRACE), tc, "register %d contents now: 0x%p", GET_UI16(cur_op, 0), reg_base[GET_UI16(cur_op, 0)].any);
                    /*(char *)(&reg_base[REGISTER_STRUCT_ACCUMULATOR]) = (char *)*/
                }
                cur_op += 12;
                goto NEXT;
            OP(eq_s):
                debugprint(DEBUG_LVL(TRACE), tc, "eq_s with %d and %d", GET_UI16(cur_op, 2), GET_UI16(cur_op, 4));
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
            OP(add_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 + GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(sub_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 - GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(mul_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 * GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(div_n):
                GET_REG(cur_op, 0).n64 = GET_REG(cur_op, 2).n64 / GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(mod_n): {
                MVMnum64 a = GET_REG(cur_op, 2).n64;
                MVMnum64 b = GET_REG(cur_op, 4).n64;
                GET_REG(cur_op, 0).n64 = b == 0 ? a : a - b * floor(a / b);
                cur_op += 6;
                goto NEXT;
            }
            OP(neg_n):
                GET_REG(cur_op, 0).n64 = -GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(abs_n):
                GET_REG(cur_op, 0).n64 = fabs(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(pow_n):
                GET_REG(cur_op, 0).n64 = pow(GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).n64);
                cur_op += 6;
                goto NEXT;
            OP(ceil_n):
                GET_REG(cur_op, 0).n64 = ceil(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(floor_n):
                GET_REG(cur_op, 0).n64 = floor(GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(coerce_in):
                GET_REG(cur_op, 0).n64 = (MVMnum64)GET_REG(cur_op, 2).i64;
                cur_op += 4;
                goto NEXT;
            OP(coerce_ni):
                GET_REG(cur_op, 0).i64 = (MVMint64)GET_REG(cur_op, 2).n64;
                cur_op += 4;
                goto NEXT;
            OP(coerce_is):
                GET_REG(cur_op, 0).s = MVM_coerce_i_s(tc, GET_REG(cur_op, 2).i64);
                cur_op += 4;
                goto NEXT;
            OP(coerce_ns):
                GET_REG(cur_op, 0).s = MVM_coerce_n_s(tc, GET_REG(cur_op, 2).n64);
                cur_op += 4;
                goto NEXT;
            OP(coerce_si):
                GET_REG(cur_op, 0).i64 = MVM_coerce_s_i(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(coerce_sn):
                GET_REG(cur_op, 0).n64 = MVM_coerce_s_n(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            OP(gt_n):
                debugprint(DEBUG_LVL(TRACE), tc, "%f > %f into %d", GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).n64, GET_UI16(cur_op, 0));
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >  GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(ge_n):
                debugprint(DEBUG_LVL(TRACE), tc, "%f >= %f into %d", GET_REG(cur_op, 2).n64, GET_REG(cur_op, 4).n64, GET_UI16(cur_op, 0));
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).n64 >=  GET_REG(cur_op, 4).n64;
                cur_op += 6;
                goto NEXT;
            OP(rand_n):
                GET_REG(cur_op, 0).n64 = MVM_proc_rand_n(tc);
                debugprint(DEBUG_LVL(TRACE), tc, "rand_n result %f into %d", GET_REG(cur_op, 0).n64, GET_UI16(cur_op, 0));
                cur_op += 2;
                goto NEXT;
            OP(goto):
                cur_op = bytecode_start + GET_UI32(cur_op, 0);
                goto NEXT;
            OP(if_i):
                if (GET_REG(cur_op, 0).i64)
                    cur_op = bytecode_start + GET_UI32(cur_op, 2);
                else
                    cur_op += 6;
                goto NEXT;
            OP(time):
                GET_REG(cur_op, 0).u64 = MVM_proc_time(tc);
                cur_op += 2;
                goto NEXT;
            OP(exit): {
                goto finish_main_loop;
            OP(index_s):
                debugprint(DEBUG_LVL(TRACE), tc, "index_s into %d with %d and %d starting at %d",
                        GET_UI16(cur_op, 0), GET_UI16(cur_op, 2), GET_UI16(cur_op, 4), GET_UI16(cur_op, 6));
                debugprint(DEBUG_LVL(TRACE), tc, "values %p and %p starting at %ld",
                        GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s, GET_REG(cur_op, 6).i64);
                GET_REG(cur_op, 0).i64 = MVM_string_index(tc,
                    STRING_OR_EMPTY(GET_REG(cur_op, 2).s), STRING_OR_EMPTY(GET_REG(cur_op, 4).s), GET_REG(cur_op, 6).i64);
                debugprint(DEBUG_LVL(TRACE), tc, "index_s result: %ld", GET_REG(cur_op, 0).i64);
                cur_op += 8;
                goto NEXT;
            OP(eqat_s):
                GET_REG(cur_op, 0).i64 = MVM_string_equal_at(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            }
            OP(eqatic_s):
                GET_REG(cur_op, 0).i64 = MVM_string_equal_at_ignore_case(tc,
                    GET_REG(cur_op, 2).s, GET_REG(cur_op, 4).s,
                    GET_REG(cur_op, 6).i64);
                cur_op += 8;
                goto NEXT;
            OP(eq_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 == GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(ne_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 != GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(lt_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <  GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(le_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 <= GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(gt_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >  GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(ge_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 >= GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(add_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 + GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(sub_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 - GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(mul_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 * GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(div_i): {
                MVMint64 num   = GET_REG(cur_op, 2).i64;
                MVMint64 denom = GET_REG(cur_op, 4).i64;
                /* if we have a negative result, make sure we floor rather
                 * than rounding towards zero. */
                if (denom == 0) {
                    fprintf(stderr, "division by zero in confprog\n");
                    goto finish_main_loop;
                }
                if ((num < 0) ^ (denom < 0)) {
                    if ((num % denom) != 0) {
                        GET_REG(cur_op, 0).i64 = num / denom - 1;
                    } else {
                        GET_REG(cur_op, 0).i64 = num / denom;
                    }
                } else {
                    GET_REG(cur_op, 0).i64 = num / denom;
                }
                cur_op += 6;
                goto NEXT;
            }
            OP(not_i): {
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 ? 0 : 1;
                cur_op += 4;
                goto NEXT;
            }
            OP(band_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 & GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bor_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 | GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(bxor_i):
                GET_REG(cur_op, 0).i64 = GET_REG(cur_op, 2).i64 ^ GET_REG(cur_op, 4).i64;
                cur_op += 6;
                goto NEXT;
            OP(getcodelocation): {
                MVMint32 line_out = 0;
                MVMString *file_out = NULL;
                MVMObject *code_obj = (MVMObject *)((MVMStaticFrame *)reg_base[REGISTER_STRUCT_ACCUMULATOR].any)->body.static_code;
                cur_op += 4;
                /* The verifier will have made sure the code doesn't end yet */
                ins = *((MVMuint16 *)cur_op);
                cur_op += 2;
                MVM_code_location_out(tc, code_obj, &file_out, &line_out);
                MVM_exception_throw_adhoc(tc, "getcodelocation in conf prog needs updates after new-disp");
                if (ins == MVM_OP_unbox_s) {
                    GET_REG(cur_op, 0).s = file_out ? file_out : tc->instance->str_consts.empty;
                }
                else {
                    GET_REG(cur_op, 0).i64 = line_out;
                }
                cur_op += 4;
                goto NEXT;
            }
            OP(chars):
                GET_REG(cur_op, 0).i64 = MVM_string_graphs(tc, GET_REG(cur_op, 2).s);
                cur_op += 4;
                goto NEXT;
            default:
                fprintf(stderr, "operation %s (%d, 0x%x) NYI\n", MVM_op_get_op(ins)->name, ins, ins);
                goto finish_main_loop;
        }
    }

finish_main_loop:

    result = reg_base[REGISTER_FEATURE_TOGGLE].i64;
    MVM_free(reg_base);

    stats_slot = stats_position_for_value(NULL, entrypoint, result);

    if (stats_slot != -1) {
        MVM_store(&prog->last_return_time[stats_slot], AO_CAST(MVM_proc_time(tc)));
        MVM_incr(&prog->return_counts[stats_slot]);
    }

    {
        const char *resultname = "unknown result meaning";
        switch (entrypoint) {
            case MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC:
            case MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC:
                switch (result) {
                    case MVM_CONFPROG_SF_RESULT_TO_BE_DETERMINED:
                        resultname = "RESULT_TO_BE_DETERMINED";
                        break;
                    case MVM_CONFPROG_SF_RESULT_NEVER:
                        resultname = "RESULT_NEVER";
                        break;
                    case MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_NO:
                        resultname = "RESULT_DYNAMIC_SUGGEST_NO";
                        break;
                    case MVM_CONFPROG_SF_RESULT_DYNAMIC_SUGGEST_YES:
                        resultname = "RESULT_DYNAMIC_SUGGEST_YES";
                        break;
                    case MVM_CONFPROG_SF_RESULT_ALWAYS:
                        resultname = "RESULT_ALWAYS";
                        break;
                }
                break;
            case MVM_PROGRAM_ENTRYPOINT_HEAPSNAPSHOT:
                switch (result) {
                    case MVM_CONFPROG_HEAPSNAPSHOT_RESULT_NOTHING:
                        resultname = "RESULT_NOTHING";
                        break;
                    case MVM_CONFPROG_HEAPSNAPSHOT_RESULT_SNAPSHOT:
                        resultname = "RESULT_SNAPSHOT";
                        break;
                    case MVM_CONFPROG_HEAPSNAPSHOT_RESULT_STATS:
                        resultname = "RESULT_STATS";
                        break;
                    case MVM_CONFPROG_HEAPSNAPSHOT_RESULT_SNAPSHOT_WITH_STATS:
                        resultname = "RESULT_SNAPSHOT_WITH_STATS";
                        break;
                }
                break;
        }
        debugprint(DEBUG_LVL(BARE), tc, "confprog result value: %"PRIi64" (%s)\n", result, resultname);
    }

    return result;
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
