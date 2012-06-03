#include "moarvm.h"

/* Some constants. */
#define HEADER_SIZE             72
#define MIN_BYTECODE_VERSION    1
#define MAX_BYTECODE_VERSION    1
#define FRAME_HEADER_SIZE       4 * 4

/* Describes the current reader state. */
typedef struct {
    /* General info. */
    MVMuint32 version;

    /* The string heap. */
    MVMuint8  *string_seg;
    MVMuint32  expected_strings;
    
    /* The frame segment. */
    MVMuint8  *frame_seg;
    MVMuint32  expected_frames;
    
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

/* Cleans up reader state. */
static void cleanup_all(MVMThreadContext *tc, ReaderState *rs) {
    free(rs);
}

/* Ensures we can read a certain amount of bytes without overrunning the end
 * of the stream. */
static void ensure_can_read(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, MVMuint8 *pos, MVMuint32 size) {
    if (pos + size > cu->data_start + cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Read past end of bytecode stream");
    }
}

/* Dissects the bytecode stream and hands back a reader pointing to the
 * various parts of it. */
static ReaderState * dissect_bytecode(MVMThreadContext *tc, MVMCompUnit *cu) {
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
    
    /* Locate frames segment. */
    offset = read_int32(cu->data_start, 28);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Frames segment starts after end of stream");
    }
    rs->frame_seg       = cu->data_start + offset;
    rs->expected_frames = read_int32(cu->data_start, 32);
    
    /* Locate strings segment. */
    offset = read_int32(cu->data_start, 48);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Strings segment starts after end of stream");
    }
    rs->string_seg       = cu->data_start + offset;
    rs->expected_strings = read_int32(cu->data_start, 52);
    
    /* Locate bytecode segment. */
    offset = read_int32(cu->data_start, 64);
    size = read_int32(cu->data_start, 68);
    if (offset > cu->data_size || offset + size > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu->data_start + offset;
    rs->bytecode_size = size;
    
    return rs;
}

/* Loads the string heap. */
static MVMString ** deserialize_strings(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMString **strings;
    MVMuint8   *pos;
    MVMuint32   i, ss;
    
    /* Allocate space for strings list. */
    if (rs->expected_strings == 0)
        return NULL;
    strings = malloc(sizeof(MVMString *) * rs->expected_strings);

    /* Load strings. */
    pos = rs->string_seg;
    for (i = 0; i < rs->expected_strings; i++) {
        /* Ensure we can read at least a string size here and do so. */
        ensure_can_read(tc, cu, rs, pos, 4);
        ss = read_int32(pos, 0);
        pos += 4;
        
        /* Ensure we can read in the string of this size, and decode
         * it if so. */
        /* XXX Should be UTF-8!! */
        ensure_can_read(tc, cu, rs, pos, ss);
        strings[i] = MVM_string_ascii_decode(tc, tc->instance->boot_types->BOOTStr, pos, ss);
        pos += ss;
        
        /* Add alignment. */
        pos += ss & 3 ? 4 - (ss & 3) : 0;
    }
    
    return strings;
}

/* Loads the static frame information (what locals we have, bytecode offset,
 * lexicals, etc.) */
static MVMStaticFrame ** deserialize_frames(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMStaticFrame **frames;
    MVMuint8        *pos;
    MVMuint32        bytecode_pos, bytecode_size, num_locals, i, j;
    
    /* Allocate frames array. */
    if (rs->expected_frames == 0) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode file must have at least one frame");
    }
    frames = malloc(sizeof(MVMStaticFrame *) * rs->expected_frames);
    
    /* Load frames. */
    pos = rs->frame_seg;
    for (i = 0; i < rs->expected_frames; i++) {
        /* Ensure we can read a frame here. */
        ensure_can_read(tc, cu, rs, pos, FRAME_HEADER_SIZE);

        /* Allocate frame and get/check bytecode start/length. */
        frames[i] = malloc(sizeof(MVMStaticFrame));
        bytecode_pos = read_int32(pos, 0);
        bytecode_size = read_int32(pos, 4);
        if (bytecode_pos >= rs->bytecode_size) {
            cleanup_all(tc, rs);
            MVM_exception_throw_adhoc(tc, "Frame has invalid bytecode start point");
        }
        if (bytecode_pos + bytecode_size > rs->bytecode_size) {
            cleanup_all(tc, rs);
            MVM_exception_throw_adhoc(tc, "Frame bytecode overflows bytecode stream");
        }
        frames[i]->bytecode = rs->bytecode_seg + bytecode_pos;
        frames[i]->bytecode_size = bytecode_size;
        
        /* Get number of locals and lexicals. */
        frames[i]->num_locals = read_int32(pos, 8);
        frames[i]->num_lexicals = read_int32(pos, 12);
        pos += FRAME_HEADER_SIZE;
        
        /* Read the local types. */
        if (frames[i]->num_locals) {
            ensure_can_read(tc, cu, rs, pos, 2 * frames[i]->num_locals);
            frames[i]->local_types = malloc(sizeof(MVMuint16) * frames[i]->num_locals);
            for (j = 0; j < frames[i]->num_locals; j++)
                frames[i]->local_types[j] = read_int16(pos, 2 * j);
            pos += 2 * frames[i]->num_locals;
        }
        
        /* Associate frame with compilation unit. */
        frames[i]->cu = cu;
    }
    
    return frames;
}

/* Takes a compilation unit pointing at a bytecode stream (which actually
 * has more than just the executive bytecode, but also various declarations,
 * like frames). Unpacks it and populates the compilation unit. */
void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu) {
    /* Dissect the bytecode into its parts. */
    ReaderState *rs = dissect_bytecode(tc, cu);
    
    /* Load the strings heap. */
    cu->strings = deserialize_strings(tc, cu, rs);
    cu->num_strings = rs->expected_strings;
    
    /* Load the static frame info. */
    cu->frames = deserialize_frames(tc, cu, rs);
    cu->num_frames = rs->expected_frames;
    
    /* Clean up reader state. */
    cleanup_all(tc, rs);
}
