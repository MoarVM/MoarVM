#ifdef PARROT_OPS_BUILD
#define PARROT_IN_EXTENSION
#include "parrot/parrot.h"
#include "parrot/extend.h"
#include "sixmodelobject.h"
#include "nodes_parrot.h"
#include "../../src/core/ops.h"
#else
#include "moarvm.h"
#endif

/* Some sizes. */
#define HEADER_SIZE             72
#define BYTECODE_VERSION        1
#define FRAME_HEADER_SIZE       4 * 4 + 2 * 2

/* Describes the state for the frame we're currently compiling. */
typedef struct {
    /* Position of start of bytecode. */
    unsigned int bytecode_start;
    
    /* Position of start of frame entry. */
    unsigned int frame_start;
    
    /* Types of locals, along with the number of them we have. */
    unsigned short *local_types;
    unsigned int num_locals;
    
    /* Labels that we have seen and know the address of. Hash of name to
     * index. */
    MASTNode *known_labels;
    
    /* Labels that are currently unresolved, that we need to fix up. Hash
     * of name to a list of positions needing a fixup. */
    MASTNode *labels_to_resolve;
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
    
    /* The compilation unit we're compiling. */
    MAST_CompUnit *cu;
} WriterState;

/* Writes an int64 into a buffer. */
static void write_int64(char *buffer, size_t offset, unsigned long long value) {
    /* XXX: Big Endian Handling! */
    memcpy(buffer + offset, &value, 8);
}

/* Writes an int32 into a buffer. */
static void write_int32(char *buffer, size_t offset, unsigned int value) {
    /* XXX: Big Endian Handling! */
    memcpy(buffer + offset, &value, 4);
}

/* Writes an int16 into a buffer. */
static void write_int16(char *buffer, size_t offset, unsigned short value) {
    /* XXX: Big Endian Handling! */
    memcpy(buffer + offset, &value, 2);
}

/* Writes an int8 into a buffer. */
static void write_int8(char *buffer, size_t offset, unsigned char value) {
    memcpy(buffer + offset, &value, 1);
}

/* Writes an double into a buffer. */
static void write_double(char *buffer, size_t offset, double value) {
    memcpy(buffer + offset, &value, 8);
}

/* Ensures the specified buffer has enough space and expands it if so. */
void ensure_space(VM, void **buffer, unsigned int *alloc, unsigned int pos, unsigned int need) {
    if (pos + need > *alloc) {
        *alloc = *alloc * 2;
        *buffer = realloc(*buffer, *alloc);
    }
}

/* Cleans up all allocated memory related to a frame. */
void cleanup_frame(VM, FrameState *fs) {
    if (fs->local_types)
        free(fs->local_types);
    free(fs);
}

/* Cleans up all allocated memory related to this compilation. */
void cleanup_all(VM, WriterState *ws) {
    if (ws->cur_frame)
        cleanup_frame(vm, ws->cur_frame);
    if (ws->frame_seg)
        free(ws->frame_seg);
    if (ws->bytecode_seg)
        free(ws->bytecode_seg);
    free(ws);
}

/* Gets the index of a string already in the string heap, or
 * adds it to the heap if it's not already there. */
unsigned short get_string_heap_index(VM, WriterState *ws, VMSTR *strval) {
    if (EXISTSKEY(vm, ws->seen_strings, strval)) {
        return (unsigned short)ATKEY_I(vm, ws->seen_strings, strval);
    }
    else {
        unsigned short index = (unsigned short)ELEMS(vm, ws->strings);
        /* XXX Overflow check */
        BINDPOS_S(interp, ws->strings, index, strval);
        BINDKEY_I(interp, ws->seen_strings, strval, index);
        return index;
    }
}

/* Takes a 6model object type and turns it into a local/lexical type flag. */
unsigned short type_to_local_type(VM, WriterState *ws, MASTNode *type) {
    MVMStorageSpec ss = REPR(type)->get_storage_spec(vm, STABLE(type));
    if (ss.inlineable) {
        switch (ss.boxed_primitive) {
            case STORAGE_SPEC_BP_INT:
                switch (ss.bits) {
                    case 8:
                        return MVM_reg_int8;
                    case 16:
                        return MVM_reg_int16;
                    case 32:
                        /* XXX Parrot specific hack... */
                        return sizeof(INTVAL) == 4 ? MVM_reg_int64 : MVM_reg_int32;
                    case 64:
                        return MVM_reg_int64;
                    default:
                        cleanup_all(vm, ws);
                        DIE(vm, "Invalid int size for local/lexical");
                }
                break;
            case STORAGE_SPEC_BP_NUM:
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
            case STORAGE_SPEC_BP_STR:
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
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
                    write_int16(ws->bytecode_seg, ws->bytecode_pos,
                        get_string_heap_index(vm, ws, sv->value));
                    ws->bytecode_pos += 2;
                }
                else {
                    cleanup_all(vm, ws);
                    DIE(vm, "Expected MAST::SVal, but didn't get one");
                }
                break;
            };
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
                    /* Find the frame index in the compilation unit. (Can
                     * probably be more efficient here later with a hash, if
                     * this becomes bottleneck...) */
                    int num_frames = ELEMS(vm, ws->cu->frames);
                    int found      = 0;
                    unsigned short i;
                    ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
                    for (i = 0; i < num_frames; i++) {
                        if (ATPOS(vm, ws->cu->frames, i) == operand) {
                            write_int16(ws->bytecode_seg, ws->bytecode_pos, i);
                            ws->bytecode_pos += 2;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        cleanup_all(vm, ws);
                        DIE(vm, "MAST::Frame passed for code ref not found in compilation unit");
                    }
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
            if (l->index > ws->cur_frame->num_locals) {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local index out of range");
            }
            
            /* Check the type matches. */
            if (op_type != ws->cur_frame->local_types[l->index] << 3) {
                cleanup_all(vm, ws);
                DIE(vm, "MAST::Local of wrong type specified");
            }
            
            /* Write the operand type. */
            ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
            write_int16(ws->bytecode_seg, ws->bytecode_pos, (unsigned char)l->index);
            ws->bytecode_pos += 2;
        }
        else {
            cleanup_all(vm, ws);
            DIE(vm, "Expected MAST::Local, but didn't get one");
        }
    }
    else {
        cleanup_all(vm, ws);
        DIE(vm, "Unknown operand type cannot be compiled");
    }
}

/* Compiles an instruction (which may actaully be any of the
 * nodes valid directly in a Frame's instruction list, which
 * means labels are valid too). */
void compile_instruction(VM, WriterState *ws, MASTNode *node) {
    if (ISTYPE(vm, node, ws->types->Op)) {
        MAST_Op   *o = GET_Op(node);
        MVMOpInfo *info;
        int        i;
        
        /* Look up opcode and get argument info. */
        unsigned char bank = (unsigned char)o->bank;
        unsigned char op   = (unsigned char)o->op;
        info = MVM_op_get_op(bank, op);
        if (!info)
            DIE(vm, "Invalid op bank specified in instruction");
        
        /* Ensure argument count matches up. */
        if (ELEMS(vm, o->operands) != info->num_operands) {
            cleanup_all(vm, ws);
            DIE(vm, "Op has invalid number of operands");
        }
        
        /* Write bank and opcode. */
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, bank);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, op);
        
        /* Write operands. */
        for (i = 0; i < info->num_operands; i++)
            compile_operand(vm, ws, info->operands[i], ATPOS(vm, o->operands, i));
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
                write_int32(ws->bytecode_seg,
                    ws->cur_frame->bytecode_start + res_pos,
                    offset);
            }
            DELETEKEY(vm, ws->cur_frame->labels_to_resolve, l->name);
        }
    }
    else if (ISTYPE(vm, node, ws->types->Call)) {
        MAST_Call *c           = GET_Call(node);
        unsigned char call_op  = MVM_OP_invoke_v;
        unsigned char res_type = 0;
        
        /* XXX Callframe handling. */
        
        /* XXX Set up args. */
        
        /* Select operation based on return type. */
        if (ISTYPE(vm, c->result, ws->types->Local)) {
            MAST_Local *l = GET_Local(c->result);
            
            /* Ensure it's within the set of known locals. */
            if (l->index > ws->cur_frame->num_locals) {
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
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 4);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, MVM_OP_BANK_primitives);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, call_op);
        if (call_op != MVM_OP_invoke_v)
            compile_operand(vm, ws, MVM_operand_read_reg | res_type, c->result);
        compile_operand(vm, ws, MVM_operand_read_reg | MVM_operand_obj, c->target);
    }
    else {
        cleanup_all(vm, ws);
        DIE(vm, "Invalid MAST node in instruction list (must be Op, Call or Label)");
    }
}

/* Compiles a frame. */
void compile_frame(VM, WriterState *ws, MASTNode *node) {
    MAST_Frame  *f;
    FrameState  *fs;
    unsigned int i, num_lexicals, num_ins;
    MASTNode *last_inst = NULL;
    
    /* Ensure we have a node of the right type. */
    if (!ISTYPE(vm, node, ws->types->Frame)) {
        cleanup_all(vm, ws);
        DIE(vm, "Child of CompUnit must be a Frame");
    }
    f = GET_Frame(node);
    
    /* Allocate frame state. */
    fs = ws->cur_frame    = malloc(sizeof(FrameState));
    fs->bytecode_start    = ws->bytecode_pos;
    fs->frame_start       = ws->frame_pos;
    fs->known_labels      = NEWHASH(vm);
    fs->labels_to_resolve = NEWHASH(vm);
    
    /* Count locals and lexicals. */
    fs->num_locals   = ELEMS(vm, f->local_types);
    num_lexicals     = ELEMS(vm, f->lexical_types);
    
    /* Ensure space is available to write frame entry, and write the
     * header, apart from the bytecode length, which we'll fill in
     * later. */
    ensure_space(vm, &ws->frame_seg, &ws->frame_alloc, ws->frame_pos,
        FRAME_HEADER_SIZE + fs->num_locals * 2 + num_lexicals * 6);
    write_int32(ws->frame_seg, ws->frame_pos, fs->bytecode_start);
    write_int32(ws->frame_seg, ws->frame_pos + 4, 0); /* Filled in later. */
    write_int32(ws->frame_seg, ws->frame_pos + 8, fs->num_locals);
    write_int32(ws->frame_seg, ws->frame_pos + 12, num_lexicals);
    write_int16(ws->frame_seg, ws->frame_pos + 16,
        get_string_heap_index(vm, ws, f->cuuid));
    write_int16(ws->frame_seg, ws->frame_pos + 18,
        get_string_heap_index(vm, ws, f->name));
    ws->frame_pos += FRAME_HEADER_SIZE;
    
    /* Write locals, as well as collecting our own array of type info. */
    fs->local_types = malloc(sizeof(unsigned short) * fs->num_locals);
    for (i = 0; i < fs->num_locals; i++) {
        unsigned short local_type = type_to_local_type(vm, ws, ATPOS(vm, f->local_types, i));
        fs->local_types[i] = local_type;
        write_int16(ws->frame_seg, ws->frame_pos, local_type);
        ws->frame_pos += 2;
    }

    /* Write lexicals. */
    if (num_lexicals)
        DIE(vm, "Cannot compile lexicals yet");

    /* Compile the instructions. */
    num_ins = ELEMS(vm, f->instructions);
    for (i = 0; i < num_ins; i++)
        compile_instruction(vm, ws, last_inst = ATPOS(vm, f->instructions, i));
    
    /* fixup frames that don't have a return instruction, so
     * we don't have to check against bytecode length every
     * time through the runloop. */
    if (!last_inst || !ISTYPE(vm, last_inst, ws->types->Op)
            || GET_Op(node)->bank != MVM_OP_BANK_primitives
            || (   GET_Op(node)->op != MVM_OP_return
                && GET_Op(node)->op != MVM_OP_return_i
                && GET_Op(node)->op != MVM_OP_return_n
                && GET_Op(node)->op != MVM_OP_return_s
                && GET_Op(node)->op != MVM_OP_return_o
            )) {
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, MVM_OP_BANK_primitives);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, MVM_OP_return);
    }
    
    /* Fill in bytecode length. */
    /* XXX */
    
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
    heap_alloc = num_strings * 16;
    heap = malloc(heap_alloc);
    
    /* Add each string to the heap. */
    for (i = 0; i < num_strings; i++) {
        /* Transcode string to UTF8. */
        STRING *utf8 = Parrot_str_change_encoding(interp,
            ATPOS_S(vm, ws->strings, i),
            Parrot_utf8_encoding_ptr->num);
        
        /* Ensure we have space. */
        unsigned int bytelen = (unsigned int)Parrot_str_byte_length(interp, utf8);
        unsigned short align = bytelen & 3 ? 4 - (bytelen & 3) : 0;
        if (heap_size + 4 + bytelen + align > heap_alloc) {
            heap_alloc *= 2;
            heap = realloc(heap, heap_alloc);
        }
        
        /* Write byte length into heap. */
        write_int32(heap, heap_size, bytelen);
        heap_size += 4;
        
        /* Write string. */
        memcpy(heap + heap_size, utf8->strstart, bytelen);
        heap_size += bytelen;
        
        /* Add alignment. */
        heap_size += align;
    }
    
    *string_heap_size = heap_size;
    return heap;
}

/* Takes all the pieces and forms the bytecode output. */
char * form_bytecode_output(VM, WriterState *ws, unsigned int *bytecode_size) {
    unsigned int size    = 0;
    unsigned int pos     = 0;
    char         *output = NULL;
    
    /* Build string heap. */
    unsigned int  string_heap_size;
    char         *string_heap = form_string_heap(vm, ws, &string_heap_size);
    
    /* Work out total size. */
    size += HEADER_SIZE;
    size += string_heap_size;
    size += ws->frame_pos;
    size += ws->bytecode_pos;
    
    /* Allocate space for the bytecode output. */
    output = malloc(size);
    memset(output, 0, size);
    
    /* Generate start of header. */
    memcpy(output, "MOARVM\r\n", 8);
    write_int32(output, 8, BYTECODE_VERSION);
    pos += HEADER_SIZE;
    
    /* Add frames section and its header entries. */
    write_int32(output, 28, pos);
    write_int32(output, 32, ws->num_frames);
    memcpy(output + pos, ws->frame_seg, ws->frame_pos);
    pos += ws->frame_pos;
    
    /* Add callsites section and its header entries. */
    write_int32(output, 36, pos);
    write_int32(output, 40, ws->num_callsites);
    memcpy(output + pos, ws->callsite_seg, ws->callsite_pos);
    pos += ws->callsite_pos;
    
    /* Add strings heap section and its header entries. */
    write_int32(output, 48, pos);
    write_int32(output, 52, ELEMS(vm, ws->strings));
    memcpy(output + pos, string_heap, string_heap_size);
    pos += string_heap_size;
    free(string_heap);
    
    /* Add bytecode section and its header entries (offset, length). */
    write_int32(output, 64, pos);
    write_int32(output, 68, ws->bytecode_pos);
    memcpy(output + pos, ws->bytecode_seg, ws->bytecode_pos);
    pos += ws->bytecode_pos;
    
    /* Sanity...should never fail. */
    if (pos != size)
        DIE(vm, "Bytecode generated did not match expected size");
    
    *bytecode_size = size;
    return output;
}

/* Main entry point to the MAST to bytecode compiler. */
char * MVM_mast_compile(VM, MASTNode *node, MASTNodeTypes *types, unsigned int *size) {
    MAST_CompUnit *cu;
    WriterState   *ws;
    char          *bytecode;
    unsigned int   i, num_frames, bytecode_size;
    
    /* Ensure we have a compilation unit. */
    if (!ISTYPE(vm, node, types->CompUnit))
        DIE(vm, "Top-level MAST node must be a CompUnit");
    cu = GET_CompUnit(node);
    
    /* Initialize the writer state structure. */
    ws = malloc(sizeof(WriterState));
    ws->types          = types;
    ws->strings        = NEWLIST_S(vm);
    ws->seen_strings   = NEWHASH(vm);
    ws->cur_frame      = NULL;
    ws->frame_pos      = 0;
    ws->frame_alloc    = 4096;
    ws->frame_seg      = malloc(ws->frame_alloc);
    ws->num_frames     = 0;
    ws->callsite_pos   = 0;
    ws->callsite_alloc = 4096;
    ws->callsite_seg   = malloc(ws->callsite_alloc);
    ws->num_callsites  = 0;
    ws->bytecode_pos   = 0;
    ws->bytecode_alloc = 4096;
    ws->bytecode_seg   = malloc(ws->bytecode_alloc);
    ws->cu             = cu;
    
    /* Visit and compile each of the frames. */
    num_frames = ELEMS(vm, cu->frames);
    for (i = 0; i < num_frames; i++)
        compile_frame(vm, ws, ATPOS(vm, cu->frames, i));
    
    /* Join all the pieces into a bytecode file. */
    bytecode = form_bytecode_output(vm, ws, &bytecode_size);
    
    /* Cleanup and hand back result. */
    cleanup_all(vm, ws);
    
    *size = bytecode_size;
    return bytecode;
}
