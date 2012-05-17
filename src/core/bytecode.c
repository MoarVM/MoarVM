#include "moarvm.h"

/* Some constants. */
#define HEADER_SIZE             72
#define MIN_BYTECODE_VERSION     1
#define MAX_BYTECODE_VERSION     1

/* Describes the current reader state. */
typedef struct {
    /* General info. */
    MVMuint32 version;
    
    /* The bytecode segment. */
    MVMuint8  *bytecode_seg;
    MVMuint32  bytecode_size;
} ReaderState;

/* Reads a uint64 from a buffer. */
static MVMuint64 read_int64(MVMuint8 *buffer, size_t offset) {
    MVMuint64 value;
    /* XXX: Big Endian Handling! */
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* Reads a uint32 from a buffer. */
static MVMuint32 read_int32(MVMuint8 *buffer, size_t offset) {
    MVMuint32 value;
    /* XXX: Big Endian Handling! */
    memcpy(&value, buffer + offset, 4);
    return value;
}

/* Reads an uint16 from a buffer. */
static MVMuint16 read_int16(MVMuint8 *buffer, size_t offset) {
    MVMuint16 value;
    /* XXX: Big Endian Handling! */
    memcpy(&value, buffer + offset, 2);
    return value;
}

/* Reads an uint8 from a buffer. */
static MVMuint8 read_int8(MVMuint8 *buffer, size_t offset) {
    return buffer[offset];
}

/* Reads double from a buffer. */
static double read_double(char *buffer, size_t offset) {
    double value;
    memcpy(&value, buffer + offset, 8);
    return value;
}

/* Disects the bytecode stream and hands back a reader pointing to the
 * various parts of it. */
static ReaderState * disect_bytecode(MVMThreadContext *tc, MVMCompUnit *cu) {
    ReaderState *rs = NULL;
    MVMuint32 version, offset, size;
    
    /* Sanity checks. */
    if (cu->data_size < HEADER_SIZE)
        MVM_exception_throw_adhoc(tc, "Bytecode stream shorter than header");
    if (memcmp(cu->data_start, "MOARVM\r\n", 8) != 0)
        MVM_exception_throw_adhoc(tc, "Bytecode stream corrupt (missing magic string)");
    version = read_int32(cu->data_start, 8);
    if (version < MIN_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too low");
    if (version > MAX_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too high");
    
    /* Allocate reader state. */
    rs = malloc(sizeof(ReaderState));
    rs->version = version;
    
    /* Locate bytecode segment. */
    offset = read_int32(cu->data_start, 64);
    size = read_int32(cu->data_start, 68);
    if (offset > cu->data_size || offset + size > cu->data_size) {
        free(rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu->data_start + offset;
    rs->bytecode_size = size;
    
    return rs;
}

/* Takes a compilation unit pointing at a bytecode stream (which actually
 * has more than just the executive bytecode, but also various declarations,
 * like frames). Unpacks it and populates the compilation unit. */
void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu) {
    /* Disect the bytecode into its parts. */
    ReaderState *rs = disect_bytecode(tc, cu);
    
    /* XXX Hack to get us running something at all. The whole stream is
     * the first static frame. */
    cu->frames = malloc(sizeof(MVMStaticFrame *));
    cu->frames[0] = malloc(sizeof(MVMStaticFrame));
    cu->frames[0]->bytecode = rs->bytecode_seg;
    cu->frames[0]->bytecode_size = rs->bytecode_size;
    cu->num_frames = 1;
    
    /* Clean up reader state. */
    /* XXX */
}
