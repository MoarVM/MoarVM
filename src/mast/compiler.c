#ifdef PARROT_OPS_BUILD
#define PARROT_IN_EXTENSION
#include "parrot/parrot.h"
#include "parrot/extend.h"
#include "sixmodelobject.h"
#include "nodes_parrot.h"
#else
#include "moarvm.h"
#endif

/* Some sizes. */
#define HEADER_SIZE             72
#define BYTECODE_VERSION        1

/* Describes the state for the frame we're currently compiling. */
typedef struct {
    /* Position of start of bytecode. */
    unsigned int bytecode_start;
} FrameState;

/* Describes the current writer state for the compilation unit as a whole. */
typedef struct {
    /* The set of node types. */
    MASTNodeTypes *types;
    
    /* The current frame. */
    FrameState *cur_frame;
    
    /* The bytecode segment. */
    char         *bytecode_seg;
    unsigned int  bytecode_pos;
    unsigned int  bytecode_alloc;
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
    free(fs);
}

/* Cleans up all allocated memory related to this compilation. */
void cleanup_all(VM, WriterState *ws) {
    if (ws->cur_frame)
        cleanup_frame(vm, ws->cur_frame);
    if (ws->bytecode_seg)
        free(ws->bytecode_seg);
    free(ws);
}

/* Compiles an instruction (which may actaully be any of the
 * nodes valid directly in a Frame's instruction list, which
 * means labels are valid too). */
void compile_instruction(VM, WriterState *ws, MASTNode *node) {
    if (ISTYPE(vm, node, ws->types->Op)) {
        MAST_Op *o = GET_Op(node);
        
        /* Look up opcode and get argument info. */
        unsigned char bank = (unsigned char)o->bank;
        unsigned char op   = (unsigned char)o->op;
        /* XXX yeah, loads to do here :-) */
        
        /* Ensure there is space to write the op. */
        ensure_space(vm, &ws->bytecode_seg, &ws->bytecode_alloc, ws->bytecode_pos, 2);
        
        /* Write bank and opcode. */
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, bank);
        write_int8(ws->bytecode_seg, ws->bytecode_pos++, op);
        
        /* Write operands. */
        /* XXX todo */
    }
    else {
        cleanup_all(vm, ws);
        DIE(vm, "Invalid MAST node in instruction list (must be Op or Frame)");
    }
}

/* Compiles a frame. */
void compile_frame(VM, WriterState *ws, MASTNode *node) {
    MAST_Frame  *f;
    FrameState  *fs;
    unsigned int i, num_ins;
    
    /* Ensure we have a node of the right type. */
    if (!ISTYPE(vm, node, ws->types->Frame)) {
        cleanup_all(vm, ws);
        DIE(vm, "Child of CompUnit must be a Frame");
    }
    f = GET_Frame(node);
    
    /* Allocate frame state. */
    fs = ws->cur_frame = malloc(sizeof(FrameState));
    fs->bytecode_start = ws->bytecode_pos;

    /* Compile the instructions. */
    num_ins = ELEMS(vm, f->instructions);
    for (i = 0; i < num_ins; i++)
        compile_instruction(vm, ws, ATPOS(vm, f->instructions, i));
    
    /* Free the frame state. */
    cleanup_frame(vm, fs);
    ws->cur_frame = NULL;
}

/* Takes all the pieces and forms the bytecode output. */
char * form_bytecode_output(VM, WriterState *ws, unsigned int *bytecode_size) {
    unsigned int size    = 0;
    unsigned int pos     = 0;
    char         *output = NULL;
    
    /* Work out total size. */
    size += HEADER_SIZE;
    size += ws->bytecode_pos;
    
    /* Allocate space for the bytecode output. */
    output = malloc(size);
    memset(output, 0, size);
    
    /* Generate start of header. */
    memcpy(output, "MOARVM\r\n", 8);
    write_int32(output, 8, BYTECODE_VERSION);
    pos += HEADER_SIZE;
    
    /* Add bytecode section and its header entries (offset, length). */
    write_int32(output, 64, pos);
    write_int32(output, 68, ws->bytecode_pos);
    memcpy(output + pos, ws->bytecode_seg, ws->bytecode_pos);
    
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
    ws->cur_frame      = NULL;
    ws->bytecode_pos   = 0;
    ws->bytecode_alloc = 4096;
    ws->bytecode_seg   = malloc(ws->bytecode_alloc);
    
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
