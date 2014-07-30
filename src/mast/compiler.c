#include "moar.h"
#include "nodes_moar.h"

#include <stdint.h>

/* Some constants. */
#define HEADER_SIZE                 92
#define BYTECODE_VERSION            3
#define FRAME_HEADER_SIZE           (9 * 4 + 2 * 2)
#define FRAME_HANDLER_SIZE          (4 * 4 + 2 * 2)
#define SC_DEP_SIZE                 4
#define EXTOP_SIZE                  (4 + 8)
#define SCDEP_HEADER_OFFSET         12
#define EXTOP_HEADER_OFFSET         20
#define FRAME_HEADER_OFFSET         28
#define CALLSITE_HEADER_OFFSET      36
#define STRING_HEADER_OFFSET        44
#define SCDATA_HEADER_OFFSET        52
#define BYTECODE_HEADER_OFFSET      60
#define ANNOTATION_HEADER_OFFSET    68
#define HLL_NAME_HEADER_OFFSET      76
#define SPECIAL_FRAME_HEADER_OFFSET 80
#define EXTOP_BASE                  1024

/* Frame flags. */
#define FRAME_FLAG_EXIT_HANDLER     1
#define FRAME_FLAG_IS_THUNK         2
#define FRAME_FLAG_HAS_INDEX        32768

typedef struct {
    /* callsite ID */
    unsigned short callsite_id;

    /* the uthash hash handle. */
    UT_hash_handle hash_handle;
} CallsiteReuseEntry;

/* Information about a handler. */
typedef struct {
    /* Offset of start of protected region from frame start. */
    unsigned int start_offset;

    /* Offset of end of protected region, exclusive, from frame start. */
    unsigned int end_offset;

    /* Exception categry mask. */
    unsigned int category_mask;

    /* Handler action. */
    unsigned short action;

    /* Local holding block to invoke, if invokey handler. */
    unsigned short local;

    /* Label, which will need resolving. */
    MASTNode *label;

    /* Local holding a label in case we have a labeled loop. */
    unsigned short label_reg;
} FrameHandler;

/* Handler actions. */
#define HANDLER_UNWIND_GOTO      0
#define HANDLER_UNWIND_GOTO_OBJ  1
#define HANDLER_INVOKE           2

/* Describes the state for the frame we're currently compiling. */
typedef struct {
    /* Position of start of bytecode. */
    unsigned int bytecode_start;

    /* Position of start of frame entry. */
    unsigned int frame_start;

    /* Types of locals, along with the number of them we have. */
    unsigned short *local_types;
    unsigned int num_locals;

    /* Types of lexicals, along with the number of them we have. */
    unsigned short *lexical_types;
    unsigned int num_lexicals;

    /* Number of annotations. */
    unsigned int num_annotations;

    /* Number of handlers */
    unsigned int num_handlers;

    /* Labels that we have seen and know the address of. Hash of name to
     * index. */
    MASTNode *known_labels;

    /* Labels that are currently unresolved, that we need to fix up. Hash
     * of name to a list of positions needing a fixup. */
    MASTNode *labels_to_resolve;

    /* Handlers list. */
    FrameHandler *handlers;

    MASTNode *frame_mast;
} FrameState;

/* Describes the current writer state for the compilation unit as a whole. */
typedef struct {
    /* The set of node types. */
    MASTNodeTypes *types;

    /* The current frame and frame count. */
    FrameState   *cur_frame;
    unsigned int  num_frames;

    /* String heap and seen hash mapping known strings to indexes. */
    MASTNode *strings;
    MASTNode *seen_strings;

    /* The SC dependencies segment; we know the size up front. */
    char         *scdep_seg;
    unsigned int  scdep_bytes;

    /* The extension ops segment; we know the size ahead of time. */
    char         *extops_seg;
    unsigned int  extops_bytes;
    unsigned int  num_extops;

    /* The frame segment. */
    char         *frame_seg;
    unsigned int  frame_pos;
    unsigned int  frame_alloc;

    /* The callsite segment and number of callsites. */
    char         *callsite_seg;
    unsigned int  callsite_pos;
    unsigned int  callsite_alloc;
    unsigned int  num_callsites;

    /* The bytecode segment. */
    char         *bytecode_seg;
    unsigned int  bytecode_pos;
    unsigned int  bytecode_alloc;

    /* The annotation segment. */
    char         *annotation_seg;
    unsigned int  annotation_pos;
    unsigned int  annotation_alloc;

    /* Current instruction info */
    MVMOpInfo    *current_op_info;

    /* Zero-based index of current frame */
    unsigned int  current_frame_idx;

    /* Zero-based index of MAST instructions */
    unsigned int  current_ins_idx;

    /* Zero-based index of current operand */
    unsigned int  current_operand_idx;

    /* The compilation unit we're compiling. */
    MAST_CompUnit *cu;

    /* Hash for callsite descriptor strings to callsite IDs */
    CallsiteReuseEntry *callsite_reuse_head;

    /* Last Annotated node, for error reporting */
    MAST_Annotated *last_annotated;
} WriterState;

static unsigned int umax(unsigned int a, unsigned int b);
static void memcpy_endian(char *dest, void *src, size_t size);
static void write_int64(char *buffer, size_t offset, unsigned long long value);
static void write_int32(char *buffer, size_t offset, unsigned int value);
static void write_int16(char *buffer, size_t offset, unsigned short value);
static void write_int8(char *buffer, size_t offset, unsigned char value);
static void write_double(char *buffer, size_t offset, double value);
void ensure_space(VM, char **buffer, unsigned int *alloc, unsigned int pos, unsigned int need);
void cleanup_frame(VM, FrameState *fs);
void cleanup_all(VM, WriterState *ws);
unsigned int get_string_heap_index(VM, WriterState *ws, VMSTR *strval);
unsigned short get_frame_index(VM, WriterState *ws, MASTNode *frame);
unsigned short type_to_local_type(VM, WriterState *ws, MASTNode *type);
void compile_operand(VM, WriterState *ws, unsigned char op_flags, MASTNode *operand);
unsigned short get_callsite_id(VM, WriterState *ws, MASTNode *flags, MASTNode *args);
void compile_instruction(VM, WriterState *ws, MASTNode *node);
void compile_frame(VM, WriterState *ws, MASTNode *node, unsigned short idx);
char * form_string_heap(VM, WriterState *ws, unsigned int *string_heap_size);
char * form_bytecode_output(VM, WriterState *ws, unsigned int *bytecode_size);
char * MVM_mast_compile(VM, MASTNode *node, MASTNodeTypes *types, unsigned int *size);

static unsigned int umax(unsigned int a, unsigned int b) {
    return a > b ? a : b;
}

/* copies memory dependent on endianness */
static void memcpy_endian(char *dest, void *src, size_t size) {
#ifdef MVM_BIGENDIAN
    size_t i;
    char *srcbytes = (char *)src;
    for (i = 0; i < size; i++)
        dest[size - i - 1] = srcbytes[i];
#else
    memcpy(dest, src, size);
#endif
}

/* Writes an int64 into a buffer. */
static void write_int64(char *buffer, size_t offset, unsigned long long value) {
    memcpy_endian(buffer + offset, &value, 8);
}

/* Writes an int32 into a buffer. */
static void write_int32(char *buffer, size_t offset, unsigned int value) {
    memcpy_endian(buffer + offset, &value, 4);
}

/* Writes an int16 into a buffer. */
static void write_int16(char *buffer, size_t offset, unsigned short value) {
    memcpy_endian(buffer + offset, &value, 2);
}

/* Writes an int8 into a buffer. */
static void write_int8(char *buffer, size_t offset, unsigned char value) {
    memcpy(buffer + offset, &value, 1);
}

/* Writes an double into a buffer. */
static void write_double(char *buffer, size_t offset, double value) {
    memcpy_endian(buffer + offset, &value, 8);
}

/* Ensures the specified buffer has enough space and expands it if so. */
void ensure_space(VM, char **buffer, unsigned int *alloc, unsigned int pos, unsigned int need) {
    if (pos + need > *alloc) {
        do { *alloc = *alloc * 2; } while (pos + need > *alloc);
        *buffer = (char *)realloc(*buffer, *alloc);
    }
}

/* Cleans up all allocated memory related to a frame. */
void cleanup_frame(VM, FrameState *fs) {
    if (fs->local_types)
        free(fs->local_types);
    if (fs->lexical_types)
        free(fs->lexical_types);
    if (fs->handlers)
        free(fs->handlers);
    free(fs);
}

/* Cleans up all allocated memory related to this compilation. */
void cleanup_all(VM, WriterState *ws) {
    if (ws->cur_frame)
        cleanup_frame(vm, ws->cur_frame);
    if (ws->scdep_seg)
        free(ws->scdep_seg);
    if (ws->extops_seg)
        free(ws->extops_seg);
    if (ws->frame_seg)
        free(ws->frame_seg);
    if (ws->callsite_seg)
        free(ws->callsite_seg);
    if (ws->bytecode_seg)
        free(ws->bytecode_seg);
    if (ws->annotation_seg)
        free(ws->annotation_seg);
    MVM_HASH_DESTROY(hash_handle, CallsiteReuseEntry, ws->callsite_reuse_head);
    free(ws);
}

/* Gets the index of a string already in the string heap, or
 * adds it to the heap if it's not already there. */
unsigned int get_string_heap_index(VM, WriterState *ws, VMSTR *strval) {
    if (EXISTSKEY(vm, ws->seen_strings, strval)) {
        return (unsigned int)ATKEY_I(vm, ws->seen_strings, strval);
    }
    else {
        unsigned int index = (unsigned int)ELEMS(vm, ws->strings);
        /* XXX Overflow check */
        BINDPOS_S(vm, ws->strings, index, strval);
        BINDKEY_I(vm, ws->seen_strings, strval, index);
        return index;
    }
}

/* Locates the index of a frame. */
unsigned short get_frame_index(VM, WriterState *ws, MASTNode *frame) {
    if (((MAST_Frame *)frame)->flags & FRAME_FLAG_HAS_INDEX) {
        return (short)((MAST_Frame *)frame)->index;
    }
    else {
        int num_frames = ELEMS(vm, ws->cu->frames);
        unsigned short i;
        for (i = 0; i < num_frames; i++)
            if (ATPOS(vm, ws->cu->frames, i) == frame)
                return i;
        cleanup_all(vm, ws);
        DIE(vm, "MAST::Frame passed for code ref not found in compilation unit");
    }
}

/* Takes a 6model object type and turns it into a local/lexical type flag. */
unsigned short type_to_local_type(VM, WriterState *ws, MASTNode *type) {
    MVMStorageSpec ss;
    if (VM_OBJ_IS_NULL(type))
        return MVM_reg_obj;
    ss = REPR(type)->get_storage_spec(vm, STABLE(type));
    if (ss.inlineable) {
        switch (ss.boxed_primitive) {
            case MVM_STORAGE_SPEC_BP_INT:
                switch (ss.bits) {
                    case 8:
                        return MVM_reg_int8;
                    case 16:
                        return MVM_reg_int16;
                    case 32:
#ifdef PARROT_OPS_BUILD
                        /* XXX Parrot specific hack... */
                        return sizeof(INTVAL) == 4 ? MVM_reg_int64 : MVM_reg_int32;
#else
                        return MVM_reg_int32;
#endif
                    case 64:
                        return MVM_reg_int64;
                    default:
                        cleanup_all(vm, ws);
                        DIE(vm, "Invalid int size for local/lexical");
                }
                break;
            case MVM_STORAGE_SPEC_BP_NUM:
                switch (ss.bits) {
                    case 32:
                        return MVM_reg_num32;
                    case 64:
                        return MVM_reg_num64;
                    default:
                        cleanup_all(vm, ws);
                        DIE(vm, "Invalid num size for local/lexical");
                }
                break;
            case MVM_STORAGE_SPEC_BP_STR:
                return MVM_reg_str;
                break;
            default:
                cleanup_all(vm, ws);
                DIE(vm, "Type used for local/lexical has invalid boxed primitive in storage spec");
        }
    }
    else {
        return MVM_reg_obj;
    }
}

/* Compiles the operand to an instruction; this involves checking
 * that we have a node of the correct type for it and writing out
 * the appropriate thing to the bytecode stream. */
void compile_operand(VM, WriterState *ws, unsigned char op_flags, MASTNode *operand) {
    unsigned char op_rw   = op_flags & MVM_operand_rw_mask;
    unsigned char op_type = op_flags & MVM_operand_type_mask;
    unsigned short int local_type;
    if (op_rw == MVM_operand_literal) {
        /* Literal; go by type. */
        switch (op_type) {
            case MVM_operand_int64: {
                if (ISTYPE(vm, operand, ws->types->IVal)) {
                    MAST_IVal *iv = GET_IVal(operand);
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 8);
                    write_int64(ws->bytecode_seg, ws->bytecode_pos, iv->value);
                    ws->bytecode_pos += 8;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::IVal, but didn't get one");
                }
                break;
            }
            case MVM_operand_int16: {
                if (ISTYPE(vm, operand, ws->types->IVal)) {
                    MAST_IVal *iv = GET_IVal(operand);
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
                    if (iv->value > 32767 || iv->value < -32768) {
                        cleanup_all(vm, ws);
                        DIE(vm, "Value outside range of 16-bit MAST::IVal");
                    }
                    write_int16(ws->bytecode_seg, ws->bytecode_pos, (short)iv->value);
                    ws->bytecode_pos += 2;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::IVal, but didn't get one");
                }
                break;
            }
            case MVM_operand_num64: {
                if (ISTYPE(vm, operand, ws->types->NVal)) {
                    MAST_NVal *nv = GET_NVal(operand);
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 8);
                    write_double(ws->bytecode_seg, ws->bytecode_pos, nv->value);
                    ws->bytecode_pos += 8;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::NVal, but didn't get one");
                }
                break;
            }
            case MVM_operand_str: {
                if (ISTYPE(vm, operand, ws->types->SVal)) {
                    MAST_SVal *sv = GET_SVal(operand);
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
                    write_int32(ws->bytecode_seg, ws->bytecode_pos,
                        get_string_heap_index(vm, ws, sv->value));
                    ws->bytecode_pos += 4;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::SVal, but didn't get one");
                }
                break;
            }
            case MVM_operand_ins: {
                if (ISTYPE(vm, operand, ws->types->Label)) {
                    MAST_Label *l = GET_Label(operand);
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
                    if (EXISTSKEY(vm, ws->cur_frame->known_labels, l->name)) {
                        /* Label offset already known; just write it. */
                        write_int32(ws->bytecode_seg, ws->bytecode_pos,
                            (unsigned int)ATKEY_I(vm, ws->cur_frame->known_labels, l->name));
                    }
                    else {
                        /* Add this as a position to fix up. */
                        MASTNode *fixup_list;
                        if (EXISTSKEY(vm, ws->cur_frame->labels_to_resolve, l->name)) {
                            fixup_list = ATKEY(vm, ws->cur_frame->labels_to_resolve, l->name);
                        }
                        else {
                            fixup_list = NEWLIST_I(vm);
                            BINDKEY(vm, ws->cur_frame->labels_to_resolve, l->name, fixup_list);
                        }
                        BINDPOS_I(vm, fixup_list, ELEMS(vm, fixup_list), ws->bytecode_pos);
                        write_int32(ws->bytecode_seg, ws->bytecode_pos, 0);
                    }
                    ws->bytecode_pos += 4;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::Label, but didn't get one");
                }
                break;
            }
            case MVM_operand_coderef: {
                if (ISTYPE(vm, operand, ws->types->Frame)) {
                    /* Find the frame index in the compilation unit. */
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
                    write_int16(ws->bytecode_seg, ws->bytecode_pos,
                        get_frame_index(vm, ws, operand));
                    ws->bytecode_pos += 2;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::Frame, but didn't get one");
                }
                break;
            }
            default:
                cleanup_all(vm, ws);
                DIE(vm, "Unhandled literal type in MAST compiler");
        }
    }
    else if (op_rw == MVM_operand_read_reg || op_rw == MVM_operand_write_reg) {
        /* The operand node had best be a MAST::Local. */
        if (ISTYPE(vm, operand, ws->types->Local)) {
            MAST_Local *l = GET_Local(operand);

            /* Ensure it's within the set of known locals. */
            if (l->index >= ws->cur_frame->num_locals) {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local index out of range");
            }

            /* Check the type matches. */
            local_type = ws->cur_frame->local_types[l->index];
            if (op_type != local_type << 3 && op_type != MVM_operand_type_var) {
                unsigned int  current_frame_idx = ws->current_frame_idx;
                unsigned int  current_ins_idx = ws->current_ins_idx;
                const char *name = ws->current_op_info->name;
                unsigned int  current_operand_idx = ws->current_operand_idx;
                cleanup_all(vm, ws);
                DIE(vm, "At Frame %u, Instruction %u, op '%s', operand %u, "
                    "MAST::Local of wrong type (%u) specified; expected %u",
                    current_frame_idx, current_ins_idx,
                    name, current_operand_idx,
                    local_type, (op_type >> 3));
            }

            /* Write the operand type. */
            if (l->index < 0 || l->index > 32768)
                DIE(vm, "Frame %u local access out of range", ws->current_frame_idx);
            ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
            write_int16(ws->bytecode_seg, ws->bytecode_pos, (unsigned short)l->index);
            ws->bytecode_pos += 2;
        }
        else {
            unsigned int  current_frame_idx = ws->current_frame_idx;
            unsigned int  current_ins_idx = ws->current_ins_idx;
            const char *name = ws->current_op_info->name;
            unsigned int  current_operand_idx = ws->current_operand_idx;
            cleanup_all(vm, ws);
            DIE(vm, "At Frame %u, Instruction %u, op '%s', operand %u, expected MAST::Local, but didn't get one",
                current_frame_idx, current_ins_idx, name, current_operand_idx);
        }
    }
    else if (op_rw == MVM_operand_read_lex || op_rw == MVM_operand_write_lex) {
        /* The operand node should be a MAST::Lexical. */
        if (ISTYPE(vm, operand, ws->types->Lexical)) {
            MAST_Lexical *l = GET_Lexical(operand);

            /* Write the index, then the frame count. */
            ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
            write_int16(ws->bytecode_seg, ws->bytecode_pos, (unsigned short)l->index);
            ws->bytecode_pos += 2;
            write_int16(ws->bytecode_seg, ws->bytecode_pos, (unsigned short)l->frames_out);
            ws->bytecode_pos += 2;
        }
        else {
            cleanup_all(vm, ws);
            DIE(vm, "Expected MAST::Lexical, but didn't get one");
        }
    }
    else {
        cleanup_all(vm, ws);
        DIE(vm, "Unknown operand type cannot be compiled");
    }
    ws->current_operand_idx++;
}

/* Takes a set of flags describing a callsite. Writes out a callsite
 * descriptor and returns the index of it. */
unsigned short get_callsite_id(VM, WriterState *ws, MASTNode *flag_node, MASTNode *args) {
    unsigned int        num_nameds = 0;
    unsigned short      i, identifier_len;
    unsigned char      *flags, *identifier;
    unsigned int       *named_idxs;
    CallsiteReuseEntry *entry = NULL;

    /* Get callsite elements and work out if a padding byte will be needed. */
    unsigned short elems = (unsigned short)ELEMS(vm, flag_node);
    unsigned short align = elems % 2;

    /* See if the callsite has any named args, and get string pool entries
     * for them if so. */
    flags      = (unsigned char *)malloc(elems);
    named_idxs = (unsigned int *)malloc(elems * sizeof(int));
    for (i = 0; i < elems; i++) {
        flags[i] = (unsigned char)ATPOS_I_C(vm, flag_node, i);
        if (flags[i] & (MVM_CALLSITE_ARG_NAMED)) {
            MASTNode *argname = ATPOS(vm, args, i + num_nameds);
            if (ISTYPE(vm, argname, ws->types->SVal)) {
                named_idxs[num_nameds] = get_string_heap_index(vm, ws,
                    ((MAST_SVal *)argname)->value);
                num_nameds++;
            }
            else {
                DIE(vm, "Malformed callsite args: missing MAST::SVal for argument name");
            }
        }
    }

    /* See if we already know this callsite. */
    identifier_len = elems + num_nameds * sizeof(int);
    identifier     = malloc(identifier_len);
    memcpy(identifier, flags, elems);
    memcpy(identifier + elems, named_idxs, identifier_len - elems);
    HASH_FIND(hash_handle, ws->callsite_reuse_head, identifier, identifier_len, entry);
    if (entry) {
        free(flags);
        free(named_idxs);
        free(identifier);
        return entry->callsite_id;
    }
    entry = (CallsiteReuseEntry *)malloc(sizeof(CallsiteReuseEntry));
    entry->callsite_id = (unsigned short)ws->num_callsites;
    HASH_ADD_KEYPTR(hash_handle, ws->callsite_reuse_head, identifier, identifier_len, entry);

    /* Emit callsite; be sure to pad if there's uneven number of flags. */
    ensure_space(vm, &ws->callsite_seg, &ws->callsite_alloc, ws->callsite_pos,
        2 + elems + align);
    write_int16(ws->callsite_seg, ws->callsite_pos, elems);
    ws->callsite_pos += 2;
    for (i = 0; i < elems; i++)
        write_int8(ws->callsite_seg, ws->callsite_pos++, flags[i]);
    if (align)
        write_int8(ws->callsite_seg, ws->callsite_pos++, 0);

    /* Emit any nameds. */
    if (num_nameds) {
        ensure_space(vm, &ws->callsite_seg, &ws->callsite_alloc, ws->callsite_pos,
            4 * num_nameds);
        for (i = 0; i < num_nameds; i++) {
            write_int32(ws->callsite_seg, ws->callsite_pos, named_idxs[i]);
            ws->callsite_pos += 4;
        }
    }

    free(flags);
    free(named_idxs);

    return (unsigned short)ws->num_callsites++;
}

#define OVERRIDE_WITH_32 1
#define OVERRIDE_WITH_16 2

/* Compiles an instruction (which may actaully be any of the
 * nodes valid directly in a Frame's instruction list, which
 * means labels are valid too). */
void compile_instruction(VM, WriterState *ws, MASTNode *node) {
    if (ISTYPE(vm, node, ws->types->Op)) {
        MAST_Op   *o = GET_Op(node);
        MVMOpInfo *info;
        int        i;
        unsigned char override_second_argument = 0;

        /* Look up opcode and get argument info. */
        unsigned short op   = o->op;
        info = MVM_op_get_op(op);
        if (!info)
            DIE(vm, "Invalid op specified in instruction %d", op);
        ws->current_op_info = info;
        ws->current_operand_idx = 0;

        /* Ensure argument count matches up. */
        if (info->num_operands != 0 && ELEMS(vm, o->operands) != info->num_operands) {
            unsigned int  current_frame_idx = ws->current_frame_idx;
            unsigned int  current_ins_idx = ws->current_ins_idx;
            const char *name = ws->current_op_info->name;
            cleanup_all(vm, ws);
            DIE(vm, "At Frame %u, Instruction %u, op '%s' has invalid number (%u) of operands; needs %u.",
                current_frame_idx, current_ins_idx, name,
                ELEMS(vm, o->operands), info->num_operands);
        }

        /* If we're outputting a const_i64 instruction, we may want to */
        /* turn it into a const_i64_32 or const_i64_16 instead if it fits */
        if (op == MVM_OP_const_i64) {
            MASTNode *operand = ATPOS(vm, o->operands, 1);
            MAST_IVal *iv = GET_IVal(operand);
            if (INT16_MIN <= iv->value && iv->value <= INT16_MAX) {
                override_second_argument = OVERRIDE_WITH_16;
            } else if (INT32_MIN <= iv->value && iv->value <= INT32_MAX) {
                override_second_argument = OVERRIDE_WITH_32;
            }
        }

        /* Write opcode. */
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        if (override_second_argument == 0)
            write_int16(ws->bytecode_seg, ws->bytecode_pos, op);
        else if (override_second_argument == OVERRIDE_WITH_16)
            write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_const_i64_16);
        else if (override_second_argument == OVERRIDE_WITH_32)
            write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_const_i64_32);
        ws->bytecode_pos += 2;

        /* Write operands. */
        for (i = 0; i < info->num_operands; i++) {
            if (i == 1 && override_second_argument) {
                MASTNode *operand = ATPOS(vm, o->operands, 1);
                MAST_IVal *iv = GET_IVal(operand);
                if (override_second_argument == OVERRIDE_WITH_32) {
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
                    write_int32(ws->bytecode_seg, ws->bytecode_pos, (MVMint32)iv->value);
                    ws->bytecode_pos += 4;
                } else {
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
                    write_int16(ws->bytecode_seg, ws->bytecode_pos, (MVMint16)iv->value);
                    ws->bytecode_pos += 2;
                }
            } else {
                compile_operand(vm, ws, info->operands[i], ATPOS(vm, o->operands, i));
            }
        }
    }
    else if (ISTYPE(vm, node, ws->types->ExtOp)) {
        MAST_ExtOp *o = GET_ExtOp(node);
        MASTNode   *operands;
        int         i, num_operands;

        /* Look up opcode and get argument info. */
        unsigned short op = o->op;
        if (op < EXTOP_BASE || (op - EXTOP_BASE) >= ELEMS(vm, ws->cu->extop_sigs))
            DIE(vm, "Invalid extension op %d specified", op);
        operands = ATPOS(vm, ws->cu->extop_sigs, op - EXTOP_BASE);
        if (VM_OBJ_IS_NULL(operands))
            DIE(vm, "Missing extension op operand array for instruction %d", op);
        ws->current_op_info = NULL;
        ws->current_operand_idx = 0;

        /* Ensure argument count matches up. */
        num_operands = ELEMS(vm, operands);
        if (ELEMS(vm, o->operands) != num_operands) {
            unsigned int  current_frame_idx = ws->current_frame_idx;
            unsigned int  current_ins_idx = ws->current_ins_idx;
            cleanup_all(vm, ws);
            DIE(vm, "At Frame %u, Instruction %u, op '%s' has invalid number (%u) of operands; needs %u.",
                current_frame_idx, current_ins_idx,
                VM_STRING_TO_C_STRING(vm, o->name),
                ELEMS(vm, o->operands), num_operands);
        }

        /* Write opcode. */
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        write_int16(ws->bytecode_seg, ws->bytecode_pos, op);
        ws->bytecode_pos += 2;

        /* Write operands. */
        for (i = 0; i < num_operands; i++)
            compile_operand(vm, ws, ATPOS_I(vm, operands, i), ATPOS(vm, o->operands, i));
    }
    else if (ISTYPE(vm, node, ws->types->Label)) {
        /* Duplicate check, then insert. */
        MAST_Label *l = GET_Label(node);
        unsigned int offset = ws->bytecode_pos - ws->cur_frame->bytecode_start;
        if (EXISTSKEY(vm, ws->cur_frame->known_labels, l->name)) {
            cleanup_all(vm, ws);
            DIE(vm, "Duplicate label");
        }
        BINDKEY_I(vm, ws->cur_frame->known_labels, l->name, offset);

        /* Resolve any existing usages. */
        if (EXISTSKEY(vm, ws->cur_frame->labels_to_resolve, l->name)) {
            MASTNode *res_list   = ATKEY(vm, ws->cur_frame->labels_to_resolve, l->name);
            unsigned int num_res = ELEMS(vm, res_list);
            unsigned int i;
            for (i = 0; i < num_res; i++) {
                unsigned int res_pos = (unsigned int)ATPOS_I(vm, res_list, i);
                write_int32(ws->bytecode_seg, res_pos, offset);
            }
            DELETEKEY(vm, ws->cur_frame->labels_to_resolve, l->name);
        }
    }
    else if (ISTYPE(vm, node, ws->types->Call)) {
        MAST_Call *c           = GET_Call(node);
        unsigned char call_op  = MVM_OP_invoke_v;
        unsigned char res_type = 0;
        unsigned short num_flags, num_args, flag_pos, arg_pos;

        /* Emit callsite (may re-use existing one) and emit loading of it. */
        unsigned short callsite_id = get_callsite_id(vm, ws, c->flags, c->args);
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
        write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_prepargs);
        ws->bytecode_pos += 2;
        write_int16(ws->bytecode_seg, ws->bytecode_pos, callsite_id);
        ws->bytecode_pos += 2;

        /* for errors */
        ws->current_op_info = MVM_op_get_op(MVM_OP_prepargs);
        ws->current_operand_idx = 0;

        /* Set up args. */
        num_flags = (unsigned short)ELEMS(vm, c->flags);
        num_args = (unsigned short)ELEMS(vm, c->args);
        arg_pos = 0;
        for (flag_pos = 0; flag_pos < num_flags; flag_pos++) {
            /* Handle any special flags. */
            unsigned char flag = (unsigned char)ATPOS_I_C(vm, c->flags, flag_pos);
            if (flag & MVM_CALLSITE_ARG_NAMED) {
                ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 6);
                write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_argconst_s);
                ws->bytecode_pos += 2;
                write_int16(ws->bytecode_seg, ws->bytecode_pos, arg_pos);
                ws->bytecode_pos += 2;
                compile_operand(vm, ws, MVM_operand_str, ATPOS(vm, c->args, arg_pos));
                arg_pos++;
            }
            else if (flag & MVM_CALLSITE_ARG_FLAT) {
                /* don't need to do anything special */
            }

            /* Now go by flag type. */
            ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 6);
            if (flag & MVM_CALLSITE_ARG_OBJ) {
                write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_arg_o);
                ws->bytecode_pos += 2;
                write_int16(ws->bytecode_seg, ws->bytecode_pos, arg_pos);
                ws->bytecode_pos += 2;
                compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_obj,
                    ATPOS(vm, c->args, arg_pos));
            }
            else if (flag & MVM_CALLSITE_ARG_STR) {
                write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_arg_s);
                ws->bytecode_pos += 2;
                write_int16(ws->bytecode_seg, ws->bytecode_pos, arg_pos);
                ws->bytecode_pos += 2;
                compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_str,
                    ATPOS(vm, c->args, arg_pos));
            }
            else if (flag & MVM_CALLSITE_ARG_INT) {
                write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_arg_i);
                ws->bytecode_pos += 2;
                write_int16(ws->bytecode_seg, ws->bytecode_pos, arg_pos);
                ws->bytecode_pos += 2;
                compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_int64,
                    ATPOS(vm, c->args, arg_pos));
            }
            else if (flag & MVM_CALLSITE_ARG_NUM) {
                write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_arg_n);
                ws->bytecode_pos += 2;
                write_int16(ws->bytecode_seg, ws->bytecode_pos, arg_pos);
                ws->bytecode_pos += 2;
                compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_num64,
                    ATPOS(vm, c->args, arg_pos));
            }
            else {
                unsigned int  current_frame_idx = ws->current_frame_idx;
                unsigned int  current_ins_idx = ws->current_ins_idx;
                const char *name = ws->current_op_info->name;
                cleanup_all(vm, ws);
                /*
                DIE(vm, "At Frame %u, Instruction %u, op '%s', "
                        "file %s, line %u, unhandled arg type %u.",
                    current_frame_idx, current_ins_idx, name,
                    ws->last_annotated ? VM_STRING_TO_C_STRING(vm, ws->last_annotated->file) : "",
                    ws->last_annotated ? ws->last_annotated->line : 0,
                    flag);
                */
                DIE(vm, "At Frame %u, Instruction %u, op '%s', unhandled arg type %u.",
                    current_frame_idx, current_ins_idx, name, flag);
            }

            arg_pos++;
        }

        /* Select operation based on return type. */
        if (ISTYPE(vm, c->result, ws->types->Local)) {
            MAST_Local *l = GET_Local(c->result);

            /* Ensure it's within the set of known locals. */
            if (l->index >= ws->cur_frame->num_locals) {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local index out of range");
            }

            /* Go by type. */
            switch (ws->cur_frame->local_types[l->index]) {
                case MVM_reg_int64:
                    call_op = MVM_OP_invoke_i;
                    res_type = MVM_operand_int64;
                    break;
                case MVM_reg_num64:
                    call_op = MVM_OP_invoke_n;
                    res_type = MVM_operand_num64;
                    break;
                case MVM_reg_str:
                    call_op = MVM_OP_invoke_s;
                    res_type = MVM_operand_str;
                    break;
                case MVM_reg_obj:
                    call_op = MVM_OP_invoke_o;
                    res_type = MVM_operand_obj;
                    break;
                default:
                    cleanup_all(vm, ws);
                    DIE(vm, "Invalid MAST::Local type for return value");
            }
        }

        /* Emit the invocation op. */
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 6);
        write_int16(ws->bytecode_seg, ws->bytecode_pos, call_op);
        ws->bytecode_pos += 2;
        if (call_op != MVM_OP_invoke_v)
            compile_operand(vm, ws, MVM_operand_read_reg | res_type, c->result);
        compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_obj, c->target);
    }
    else if (ISTYPE(vm, node, ws->types->Annotated)) {
        MAST_Annotated *a = GET_Annotated(node);
        unsigned int i;
        unsigned int num_ins = ELEMS(vm, a->instructions);
        unsigned int offset = ws->bytecode_pos - ws->cur_frame->bytecode_start;

        ws->last_annotated = a;
        ensure_space(vm, &ws->annotation_seg, &ws->annotation_alloc, ws->annotation_pos, 12);
        write_int32(ws->annotation_seg, ws->annotation_pos, offset);
        write_int32(ws->annotation_seg, ws->annotation_pos + 4, get_string_heap_index(vm, ws, a->file));
        write_int32(ws->annotation_seg, ws->annotation_pos + 8, (unsigned int)a->line);
        ws->annotation_pos += 12;
        ws->cur_frame->num_annotations++;

        for (i = 0; i < num_ins; i++)
            compile_instruction(vm, ws, ATPOS(vm, a->instructions, i));
    }
    else if (ISTYPE(vm, node, ws->types->HandlerScope)) {
        MAST_HandlerScope *hs = GET_HandlerScope(node);
        unsigned int i;
        unsigned int num_ins = ELEMS(vm, hs->instructions);
        unsigned int start   = ws->bytecode_pos - ws->cur_frame->bytecode_start;
        unsigned int end;

        for (i = 0; i < num_ins; i++)
            compile_instruction(vm, ws, ATPOS(vm, hs->instructions, i));
        end = ws->bytecode_pos - ws->cur_frame->bytecode_start;

        ws->cur_frame->num_handlers++;
        if (ws->cur_frame->handlers)
            ws->cur_frame->handlers = (FrameHandler *)realloc(ws->cur_frame->handlers,
                ws->cur_frame->num_handlers * sizeof(FrameHandler));
        else
            ws->cur_frame->handlers = (FrameHandler *)malloc(
                ws->cur_frame->num_handlers * sizeof(FrameHandler));

        i = ws->cur_frame->num_handlers - 1;
        ws->cur_frame->handlers[i].start_offset = start;
        ws->cur_frame->handlers[i].end_offset = end;
        ws->cur_frame->handlers[i].category_mask = (unsigned int)hs->category_mask;
        ws->cur_frame->handlers[i].action = (unsigned short)hs->action;
        if (ws->cur_frame->handlers[i].category_mask & MVM_EX_CAT_LABELED) {
            if (ISTYPE(vm, hs->label_local, ws->types->Local)) {
                MAST_Local *l = GET_Local(hs->label_local);

                /* Ensure it's within the set of known locals and an object. */
                if (l->index >= ws->cur_frame->num_locals) {
                    cleanup_all(vm, ws);
                    DIE(vm, "MAST::Local index out of range in HandlerScope");
                }
                if (ws->cur_frame->local_types[l->index] != MVM_reg_obj) {
                    cleanup_all(vm, ws);
                    DIE(vm, "MAST::Local for HandlerScope must be an object");
                }

                /* Stash local index. */
                ws->cur_frame->handlers[i].label_reg = (unsigned short)l->index;
            }
            else {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local required for HandlerScope with loop label");
            }
        }

        /* Ensure we have a label. */
        if (ISTYPE(vm, hs->goto_label, ws->types->Label)) {
            ws->cur_frame->handlers[i].label = hs->goto_label;
        }
        else {
            cleanup_all(vm, ws);
            DIE(vm, "MAST::Label required for HandlerScope goto");
        }

        /* May need a block also. */
        if (hs->action == HANDLER_INVOKE) {
            if (ISTYPE(vm, hs->block_local, ws->types->Local)) {
                MAST_Local *l = GET_Local(hs->block_local);

                /* Ensure it's within the set of known locals and an object. */
                if (l->index >= ws->cur_frame->num_locals) {
                    cleanup_all(vm, ws);
                    DIE(vm, "MAST::Local index out of range in HandlerScope");
                }
                if (ws->cur_frame->local_types[l->index] != MVM_reg_obj) {
                    cleanup_all(vm, ws);
                    DIE(vm, "MAST::Local for HandlerScope must be an object");
                }

                /* Stash local index. */
                ws->cur_frame->handlers[i].local = (unsigned short)l->index;
            }
            else {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local required for HandlerScope invoke action");
            }
        }
        else if (hs->action == HANDLER_UNWIND_GOTO || hs->action == HANDLER_UNWIND_GOTO_OBJ) {
            ws->cur_frame->handlers[i].local = 0;
        }
        else {
            cleanup_all(vm, ws);
            DIE(vm, "Invalid action code for handler scope");
        }
    }
    else {
        cleanup_all(vm, ws);
        DIE(vm, "Invalid MAST node in instruction list (must be Op, ExtOp, Call, Label, or Annotated)");
    }
    ws->current_ins_idx++;
}

/* Compiles a frame. */
void compile_frame(VM, WriterState *ws, MASTNode *node, unsigned short idx) {
    MAST_Frame  *f;
    FrameState  *fs;
    unsigned int i, num_ins, instructions_start;
    MASTNode *last_inst = NULL;

    /* Ensure we have a node of the right type. */
    if (!ISTYPE(vm, node, ws->types->Frame)) {
        cleanup_all(vm, ws);
        DIE(vm, "Child of CompUnit must be a Frame");
    }
    f = GET_Frame(node);

    /* Allocate frame state. */
    fs = ws->cur_frame    = (FrameState *)malloc(sizeof(FrameState));
    fs->bytecode_start    = ws->bytecode_pos;
    fs->frame_start       = ws->frame_pos;
    fs->known_labels      = NEWHASH(vm);
    fs->labels_to_resolve = NEWHASH(vm);

    /* Count locals and lexicals. */
    fs->num_locals   = ELEMS(vm, f->local_types);
    fs->num_lexicals = ELEMS(vm, f->lexical_types);

    if (fs->num_locals > (1 << 16)) {
        cleanup_all(vm, ws);
        DIE(vm, "Too many locals in this frame.");
    }

    if (ELEMS(vm, f->lexical_names) != fs->num_lexicals) {
        cleanup_all(vm, ws);
        DIE(vm, "Lexical types list and lexical names list have unequal length");
    }

    /* initialize number of annotation */
    fs->num_annotations = 0;

    /* initialize number of handlers and handlers pointer */
    fs->num_handlers = 0;
    fs->handlers = NULL;

    /* Ensure space is available to write frame entry, and write the
     * header, apart from the bytecode length, which we'll fill in
     * later. */
    ensure_space(vm, &ws->frame_seg, &ws->frame_alloc, ws->frame_pos,
        FRAME_HEADER_SIZE + fs->num_locals * 2 + fs->num_lexicals * 6);
    write_int32(ws->frame_seg, ws->frame_pos, fs->bytecode_start);
    write_int32(ws->frame_seg, ws->frame_pos + 4, 0); /* Filled in later. */
    write_int32(ws->frame_seg, ws->frame_pos + 8, fs->num_locals);
    write_int32(ws->frame_seg, ws->frame_pos + 12, fs->num_lexicals);
    write_int32(ws->frame_seg, ws->frame_pos + 16,
        get_string_heap_index(vm, ws, f->cuuid));
    write_int32(ws->frame_seg, ws->frame_pos + 20,
        get_string_heap_index(vm, ws, f->name));

    /* Handle outer. The current index means "no outer". */
    if (ISTYPE(vm, f->outer, ws->types->Frame)) {
        unsigned short i, found, num_frames;
        found = 0;
        num_frames = (unsigned short)ELEMS(vm, ws->cu->frames);
        for (i = 0; i < num_frames; i++) {
            if (ATPOS(vm, ws->cu->frames, i) == f->outer) {
                write_int16(ws->frame_seg, ws->frame_pos + 24, i);
                found = 1;
                break;
            }
        }
        if (!found) {
            cleanup_all(vm, ws);
            DIE(vm, "Could not locate outer frame in frame list");
        }
    }
    else {
        write_int16(ws->frame_seg, ws->frame_pos + 24, idx);
    }

    write_int32(ws->frame_seg, ws->frame_pos + 26, ws->annotation_pos);
    write_int32(ws->frame_seg, ws->frame_pos + 30, 0); /* number of annotation; fill in later */
    write_int32(ws->frame_seg, ws->frame_pos + 34, 0); /* number of handlers; fill in later */
    write_int16(ws->frame_seg, ws->frame_pos + 38, (MVMint16)f->flags);

    ws->frame_pos += FRAME_HEADER_SIZE;

    /* Write locals, as well as collecting our own array of type info. */
    fs->local_types = (short unsigned int *)malloc(sizeof(unsigned short) * fs->num_locals);
    for (i = 0; i < fs->num_locals; i++) {
        unsigned short local_type = type_to_local_type(vm, ws, ATPOS(vm, f->local_types, i));
        fs->local_types[i] = local_type;
        write_int16(ws->frame_seg, ws->frame_pos, local_type);
        ws->frame_pos += 2;
    }

    /* Write lexicals. */
    fs->lexical_types = (short unsigned int *)malloc(sizeof(unsigned short) * fs->num_lexicals);
    for (i = 0; i < fs->num_lexicals; i++) {
        unsigned short lexical_type = type_to_local_type(vm, ws, ATPOS(vm, f->lexical_types, i));
        fs->lexical_types[i] = lexical_type;
        write_int16(ws->frame_seg, ws->frame_pos, lexical_type);
        ws->frame_pos += 2;
        write_int32(ws->frame_seg, ws->frame_pos,
            get_string_heap_index(vm, ws, ATPOS_S_C(vm, f->lexical_names, i)));
        ws->frame_pos += 4;
    }

    /* Save the location of the start of instructions */
    instructions_start = ws->bytecode_pos;

    /* Compile the instructions. */
    ws->current_ins_idx = 0;
    num_ins = ELEMS(vm, f->instructions);
    for (i = 0; i < num_ins; i++)
        compile_instruction(vm, ws, last_inst = ATPOS(vm, f->instructions, i));

    /* fixup frames that don't have a return instruction, so
     * we don't have to check against bytecode length every
     * time through the runloop. */
    if (!last_inst || !ISTYPE(vm, last_inst, ws->types->Op)
            || (   GET_Op(last_inst)->op != MVM_OP_return
                && GET_Op(last_inst)->op != MVM_OP_return_i
                && GET_Op(last_inst)->op != MVM_OP_return_n
                && GET_Op(last_inst)->op != MVM_OP_return_s
                && GET_Op(last_inst)->op != MVM_OP_return_o
            )) {
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        write_int16(ws->bytecode_seg, ws->bytecode_pos, MVM_OP_return);
        ws->bytecode_pos += 2;
    }

    /* Fill in bytecode length. */
    write_int32(ws->frame_seg, fs->frame_start + 4, ws->bytecode_pos - instructions_start);

    /* Fill in number of annotations. */
    write_int32(ws->frame_seg, fs->frame_start + 30, fs->num_annotations);

    /* Fill in number of handlers. */
    write_int32(ws->frame_seg, fs->frame_start + 34, fs->num_handlers);

    /* Write handlers. */
    ensure_space(vm, &ws->frame_seg, &ws->frame_alloc, ws->frame_pos,
        FRAME_HANDLER_SIZE * fs->num_handlers);
    for (i = 0; i < fs->num_handlers; i++) {
        write_int32(ws->frame_seg, ws->frame_pos, fs->handlers[i].start_offset);
        ws->frame_pos += 4;
        write_int32(ws->frame_seg, ws->frame_pos, fs->handlers[i].end_offset);
        ws->frame_pos += 4;
        write_int32(ws->frame_seg, ws->frame_pos, fs->handlers[i].category_mask);
        ws->frame_pos += 4;
        write_int16(ws->frame_seg, ws->frame_pos, fs->handlers[i].action);
        ws->frame_pos += 2;
        write_int16(ws->frame_seg, ws->frame_pos, fs->handlers[i].local);
        ws->frame_pos += 2;
        if (ws->cur_frame->handlers[i].label) {
            MAST_Label *l = GET_Label(fs->handlers[i].label);
            if (EXISTSKEY(vm, fs->known_labels, l->name)) {
                write_int32(ws->frame_seg, ws->frame_pos,
                    (unsigned int)ATKEY_I(vm, fs->known_labels, l->name));
            }
            else {
                cleanup_all(vm, ws);
                    DIE(vm, "HandlerScope uses unresolved label");
            }
        }
        else {
            write_int32(ws->frame_seg, ws->frame_pos, 0);
        }
        ws->frame_pos += 4;
        if (fs->handlers[i].category_mask & MVM_EX_CAT_LABELED) {
            write_int16(ws->frame_seg, ws->frame_pos, fs->handlers[i].label_reg);
            ws->frame_pos += 2;
        }
    }

    /* Any leftover labels? */
    if (HASHELEMS(vm, fs->labels_to_resolve)) {
        cleanup_all(vm, ws);
        DIE(vm, "Frame has unresolved labels");
    }

    /* Free the frame state. */
    cleanup_frame(vm, fs);
    ws->cur_frame = NULL;

    /* Increment frame count. */
    ws->num_frames++;
}

/* Takes all of the strings and joins them into a heap, encoding them as
 * UTF-8. */
char * form_string_heap(VM, WriterState *ws, unsigned int *string_heap_size) {
    char         *heap;
    unsigned int  i, num_strings, heap_size, heap_alloc;

    /* If we've nothing to do, just return immediately. */
    num_strings = ELEMS(vm, ws->strings);
    if (num_strings == 0) {
        *string_heap_size = 0;
        return NULL;
    }

    /* Allocate heap starting point (just a guess). */
    heap_size = 0;
    heap_alloc = num_strings * 32;
    heap = (char *)malloc(heap_alloc);

    /* Add each string to the heap. */
    for (i = 0; i < num_strings; i++) {
#ifdef PARROT_OPS_BUILD
        /* Transcode string to UTF8. */
        STRING *utf8 = Parrot_str_change_encoding(interp,
            ATPOS_S(vm, ws->strings, i),
            Parrot_utf8_encoding_ptr->num);
        unsigned int bytelen = (unsigned int)Parrot_str_byte_length(interp, utf8);
#else
        MVMuint64 bytelen;
        MVMuint8 *utf8 = MVM_string_utf8_encode(tc, ATPOS_S(vm, ws->strings, i), &bytelen);
#endif

        /* Ensure we have space. */
        unsigned short align = bytelen & 3 ? 4 - (bytelen & 3) : 0;
        unsigned int   need  = 4 + bytelen + align;
        if (heap_size + need >= heap_alloc) {
            heap_alloc = umax(heap_alloc * 2, heap_size + need);
            heap = (char *)realloc(heap, heap_alloc);
        }

        /* Write byte length into heap. */
        write_int32(heap, heap_size, bytelen);
        heap_size += 4;

        /* Write string. */
#ifdef PARROT_OPS_BUILD
        memcpy(heap + heap_size, utf8->strstart, bytelen);
#else
        memcpy(heap + heap_size, utf8, bytelen);
        free(utf8);
#endif
        heap_size += bytelen;

        /* Add alignment. Whilst we never read this memory, it's useful to
           ensure it is initialised, otherwise valgrind (and similar tools)
           will rightly complain that we're writing garbage to disk. */
        if (align) {
            memset(heap + heap_size, 0, align);
            heap_size += align;
        }
    }

    *string_heap_size = heap_size;
    return heap;
}

/* Takes all the pieces and forms the bytecode output. */
char * form_bytecode_output(VM, WriterState *ws, unsigned int *bytecode_size) {
    unsigned int size    = 0;
    unsigned int pos     = 0;
    char         *output = NULL;
    unsigned int  string_heap_size;
    char         *string_heap;
    unsigned int  hll_str_idx;

    /* Store HLL name string, if any. */
    if (!VM_STRING_IS_NULL(ws->cu->hll))
        hll_str_idx = get_string_heap_index(vm, ws, ws->cu->hll);
    else
        hll_str_idx = get_string_heap_index(vm, ws, EMPTY_STRING(vm));

    /* Build string heap. */
    string_heap = form_string_heap(vm, ws, &string_heap_size);

    /* Work out total size. */
    size += HEADER_SIZE;
    size += string_heap_size;
    size += ws->scdep_bytes;
    size += ws->extops_bytes;
    size += ws->frame_pos;
    size += ws->callsite_pos;
    size += ws->bytecode_pos;
    size += ws->annotation_pos;
    if (vm->serialized)
        size += vm->serialized_size;

    /* Allocate space for the bytecode output. */
    output = (char *)malloc(size);
    memset(output, 0, size);

    /* Generate start of header. */
    memcpy(output, "MOARVM\r\n", 8);
    write_int32(output, 8, BYTECODE_VERSION);
    pos += HEADER_SIZE;

    /* Add SC dependencies section and its header entries. */
    write_int32(output, SCDEP_HEADER_OFFSET, pos);
    write_int32(output, SCDEP_HEADER_OFFSET + 4, ELEMS(vm, ws->cu->sc_handles));
    memcpy(output + pos, ws->scdep_seg, ws->scdep_bytes);
    pos += ws->scdep_bytes;

    /* Add extension ops section and its header entries. */
    write_int32(output, EXTOP_HEADER_OFFSET, pos);
    write_int32(output, EXTOP_HEADER_OFFSET + 4, ws->num_extops);
    memcpy(output + pos, ws->extops_seg, ws->extops_bytes);
    pos += ws->extops_bytes;

    /* Add frames section and its header entries. */
    write_int32(output, FRAME_HEADER_OFFSET, pos);
    write_int32(output, FRAME_HEADER_OFFSET + 4, ws->num_frames);
    memcpy(output + pos, ws->frame_seg, ws->frame_pos);
    pos += ws->frame_pos;

    /* Add callsites section and its header entries. */
    write_int32(output, CALLSITE_HEADER_OFFSET, pos);
    write_int32(output, CALLSITE_HEADER_OFFSET + 4, ws->num_callsites);
    memcpy(output + pos, ws->callsite_seg, ws->callsite_pos);
    pos += ws->callsite_pos;

    /* Add strings heap section and its header entries. */
    write_int32(output, STRING_HEADER_OFFSET, pos);
    write_int32(output, STRING_HEADER_OFFSET + 4, ELEMS(vm, ws->strings));
    memcpy(output + pos, string_heap, string_heap_size);
    pos += string_heap_size;
    if (string_heap) {
        free(string_heap);
        string_heap = NULL;
    }

    /* SC data. Write it if we have it. */
    if (vm->serialized) {
        write_int32(output, SCDATA_HEADER_OFFSET, pos);
        write_int32(output, SCDATA_HEADER_OFFSET + 4, vm->serialized_size);
        memcpy(output + pos, vm->serialized, vm->serialized_size);
        pos += vm->serialized_size;
        free(vm->serialized);
        vm->serialized = NULL;
        vm->serialized_size = 0;
    }

    /* Add bytecode section and its header entries (offset, length). */
    write_int32(output, BYTECODE_HEADER_OFFSET, pos);
    write_int32(output, BYTECODE_HEADER_OFFSET + 4, ws->bytecode_pos);
    memcpy(output + pos, ws->bytecode_seg, ws->bytecode_pos);
    pos += ws->bytecode_pos;

    /* Add annotation section and its header entries (offset, length). */
    write_int32(output, ANNOTATION_HEADER_OFFSET, pos);
    write_int32(output, ANNOTATION_HEADER_OFFSET + 4, ws->annotation_pos);
    memcpy(output + pos, ws->annotation_seg, ws->annotation_pos);
    pos += ws->annotation_pos;

    /* Add HLL and special frame indexes. */
    write_int32(output, HLL_NAME_HEADER_OFFSET, hll_str_idx);
    if (VM_OBJ_IS_NULL(ws->cu->main_frame))
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET, 0);
    else
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET, 1 + get_frame_index(vm, ws, ws->cu->main_frame));
    if (VM_OBJ_IS_NULL(ws->cu->load_frame))
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET + 4, 0);
    else
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET + 4, 1 + get_frame_index(vm, ws, ws->cu->load_frame));
    if (VM_OBJ_IS_NULL(ws->cu->deserialize_frame))
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET + 8, 0);
    else
        write_int32(output, SPECIAL_FRAME_HEADER_OFFSET + 8, 1 + get_frame_index(vm, ws, ws->cu->deserialize_frame));

    /* Sanity...should never fail. */
    if (pos != size)
        DIE(vm, "Bytecode generated did not match expected size");

    *bytecode_size = size;
    return output;
}

/* Main entry point to the MAST to bytecode compiler. */
char * MVM_mast_compile(VM, MASTNode *node, MASTNodeTypes *types, unsigned int *size) {
    MAST_CompUnit  *cu;
    WriterState    *ws;
    char           *bytecode;
    unsigned short  i, num_depscs, num_frames;
    unsigned int    bytecode_size;

    /* Ensure we have a compilation unit. */
    if (!ISTYPE(vm, node, types->CompUnit))
        DIE(vm, "Top-level MAST node must be a CompUnit");
    cu = GET_CompUnit(node);

    /* Initialize the writer state structure. */
    ws = (WriterState *)malloc(sizeof(WriterState));
    ws->types            = types;
    ws->strings          = NEWLIST_S(vm);
    ws->seen_strings     = NEWHASH(vm);
    ws->cur_frame        = NULL;
    ws->scdep_bytes      = ELEMS(vm, cu->sc_handles) * SC_DEP_SIZE;
    ws->scdep_seg        = ws->scdep_bytes ? (char *)malloc(ws->scdep_bytes) : NULL;
    ws->num_extops       = ELEMS(vm, cu->extop_names);
    ws->extops_bytes     = ws->num_extops * EXTOP_SIZE;
    ws->extops_seg       = (char *)malloc(ws->extops_bytes);
    ws->frame_pos        = 0;
    ws->frame_alloc      = 4096;
    ws->frame_seg        = (char *)malloc(ws->frame_alloc);
    ws->num_frames       = 0;
    ws->callsite_pos     = 0;
    ws->callsite_alloc   = 4096;
    ws->callsite_seg     = (char *)malloc(ws->callsite_alloc);
    ws->num_callsites    = 0;
    ws->bytecode_pos     = 0;
    ws->bytecode_alloc   = 4096;
    ws->bytecode_seg     = (char *)malloc(ws->bytecode_alloc);
    ws->annotation_pos   = 0;
    ws->annotation_alloc = 4096;
    ws->annotation_seg   = (char *)malloc(ws->annotation_alloc);
    ws->cu               = cu;
    ws->current_frame_idx= 0;

    /* initialize callsite reuse cache */
    ws->callsite_reuse_head = NULL;

    /* Store each of the dependent SCs. */
    num_depscs = ELEMS(vm, ws->cu->sc_handles);
    for (i = 0; i < num_depscs; i++)
        write_int32(ws->scdep_seg, i * SC_DEP_SIZE,
            get_string_heap_index(vm, ws,
                ATPOS_S_C(vm, ws->cu->sc_handles, i)));

    /* Store each of the extop names and signatures. */
    for (i = 0; i < ws->num_extops; i++) {
        MASTNode *sig_array;
        int num_operands, j;

        write_int32(ws->extops_seg, i * EXTOP_SIZE,
            get_string_heap_index(vm, ws,
                ATPOS_S_C(vm, ws->cu->extop_names, i)));

        sig_array = ATPOS(vm, ws->cu->extop_sigs, i);
        num_operands = ELEMS(vm, sig_array);
        for (j = 0; j < 8; j++)
            write_int8(ws->extops_seg, i * EXTOP_SIZE + 4 + j,
                j < num_operands
                    ? ATPOS_I(vm, sig_array, j)
                    : 0);
    }

    /* Visit and compile each of the frames. */
    num_frames = (unsigned short)ELEMS(vm, cu->frames);
    for (i = 0; i < num_frames; i++)
        compile_frame(vm, ws, ATPOS(vm, cu->frames, i), ws->current_frame_idx = i);

    /* Join all the pieces into a bytecode file. */
    bytecode = form_bytecode_output(vm, ws, &bytecode_size);

    /* Cleanup and hand back result. */
    cleanup_all(vm, ws);

    *size = bytecode_size;
    return bytecode;
}
