#include "moarvm.h"

/* Some constants. */
#define HEADER_SIZE             88
#define MIN_BYTECODE_VERSION    1
#define MAX_BYTECODE_VERSION    1
#define FRAME_HEADER_SIZE       7 * 4 + 3 * 2
#define FRAME_HANDLER_SIZE      4 * 4 + 2 * 2

/* Describes the current reader state. */
typedef struct {
    /* General info. */
    MVMuint32 version;

    /* The string heap. */
    MVMuint8  *string_seg;
    MVMuint32  expected_strings;

    /* The SC dependencies segment. */
    MVMuint8  *sc_seg;
    MVMuint32  expected_scs;

    /* The frame segment. */
    MVMuint8  *frame_seg;
    MVMuint32  expected_frames;
    MVMuint16 *frame_outer_fixups;

    /* The callsites segment. */
    MVMuint8  *callsite_seg;
    MVMuint32  expected_callsites;

    /* The bytecode segment. */
    MVMuint8  *bytecode_seg;
    MVMuint32  bytecode_size;

    /* The annotations segment */
    MVMuint8  *annotation_seg;
    MVMuint32  annotation_size;

    /* HLL name string index */
    MVMuint32  hll_str_idx;

    /* Special frame indexes */
    MVMuint32  main_frame;
    MVMuint32  load_frame;
    MVMuint32  deserialize_frame;

} ReaderState;

/* copies memory dependent on endianness */
static void memcpy_endian(void *dest, MVMuint8 *src, size_t size) {
#ifdef MVM_BIGENDIAN
    size_t i;
    MVMuint8 *destbytes = (MVMuint8 *)dest;
    for (i = 0; i < size; i++)
        destbytes[size - i - 1] = src[i];
#else
    memcpy(dest, src, size);
#endif
}

/* Reads a uint64 from a buffer. */
static MVMuint64 read_int64(MVMuint8 *buffer, size_t offset) {
    MVMuint64 value;
    memcpy_endian(&value, buffer + offset, 8);
    return value;
}

/* Reads a uint32 from a buffer. */
static MVMuint32 read_int32(MVMuint8 *buffer, size_t offset) {
    MVMuint32 value;
    memcpy_endian(&value, buffer + offset, 4);
    return value;
}

/* Reads an uint16 from a buffer. */
static MVMuint16 read_int16(MVMuint8 *buffer, size_t offset) {
    MVMuint16 value;
    memcpy_endian(&value, buffer + offset, 2);
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
    if (rs->frame_outer_fixups) {
        free(rs->frame_outer_fixups);
        rs->frame_outer_fixups = NULL;
    }
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

/* Reads a string index, looks up the string and returns it. Bounds
 * checks the string heap index too. */
static MVMString * get_heap_string(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, char *buffer, size_t offset) {
    MVMuint16 heap_index = read_int16(buffer, offset);
    if (heap_index >= cu->num_strings) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "String heap index beyond end of string heap");
    }
    return cu->strings[heap_index];
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

    /* Locate SC dependencies segment. */
    offset = read_int32(cu->data_start, 12);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Serialization contexts segment starts after end of stream");
    }
    rs->sc_seg       = cu->data_start + offset;
    rs->expected_scs = read_int32(cu->data_start, 16);

    /* Locate frames segment. */
    offset = read_int32(cu->data_start, 20);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Frames segment starts after end of stream");
    }
    rs->frame_seg       = cu->data_start + offset;
    rs->expected_frames = read_int32(cu->data_start, 24);

    /* Locate callsites segment. */
    offset = read_int32(cu->data_start, 28);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Callsites segment starts after end of stream");
    }
    rs->callsite_seg       = cu->data_start + offset;
    rs->expected_callsites = read_int32(cu->data_start, 32);

    /* Locate strings segment. */
    offset = read_int32(cu->data_start, 40);
    if (offset > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Strings segment starts after end of stream");
    }
    rs->string_seg       = cu->data_start + offset;
    rs->expected_strings = read_int32(cu->data_start, 44);

    /* Locate bytecode segment. */
    offset = read_int32(cu->data_start, 56);
    size = read_int32(cu->data_start, 60);
    if (offset > cu->data_size || offset + size > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu->data_start + offset;
    rs->bytecode_size = size;

    /* Locate annotations segment. */
    offset = read_int32(cu->data_start, 64);
    size = read_int32(cu->data_start, 68);
    if (offset > cu->data_size || offset + size > cu->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Annotation segment overflows end of stream");
    }
    rs->annotation_seg  = cu->data_start + offset;
    rs->annotation_size = size;

    /* Locate HLL name */
    rs->hll_str_idx = read_int32(cu->data_start, 72);

    /* Locate special frame indexes. Note, they are 0 for none, and the
     * index + 1 if there is one. */
    rs->main_frame = read_int32(cu->data_start, 76);
    rs->load_frame = read_int32(cu->data_start, 80);
    rs->deserialize_frame = read_int32(cu->data_start, 84);
    if (rs->main_frame > rs->expected_frames
            || rs->load_frame > rs->expected_frames
            || rs->deserialize_frame > rs->expected_frames) {
        MVM_exception_throw_adhoc(tc, "Special frame index out of bounds");
    }

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
        ensure_can_read(tc, cu, rs, pos, ss);
        strings[i] = MVM_string_utf8_decode(tc, tc->instance->VMString, pos, ss);
        pos += ss;

        /* Add alignment. */
        pos += ss & 3 ? 4 - (ss & 3) : 0;
    }

    return strings;
}

/* Loads the SC dependencies list. */
static void deserialize_sc_deps(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMuint32 i, sh_idx;
    MVMuint8  *pos;

    /* Allocate SC lists in compilation unit. */
    cu->scs = malloc(rs->expected_scs * sizeof(MVMSerializationContext *));
    cu->scs_to_resolve = malloc(rs->expected_scs * sizeof(MVMString *));
    cu->num_scs = rs->expected_scs;

    /* Resolve all the things. */
    pos = rs->sc_seg;
    for (i = 0; i < rs->expected_scs; i++) {
        MVMSerializationContextBody *scb;
        MVMString *handle;

        /* Grab string heap index. */
        ensure_can_read(tc, cu, rs, pos, 4);
        sh_idx = read_int32(pos, 0);
        pos += 4;

        /* Resolve to string. */
        if (sh_idx >= cu->num_strings) {
            cleanup_all(tc, rs);
            MVM_exception_throw_adhoc(tc, "String heap index beyond end of string heap");
        }
        handle = cu->strings[sh_idx];

        /* See if we can resolve it. */
        if (apr_thread_mutex_lock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
            MVM_exception_throw_adhoc(tc, "Unable to lock SC weakhash");
        MVM_string_flatten(tc, handle);
        MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
        if (scb) {
            cu->scs_to_resolve[i] = NULL;
            cu->scs[i] = scb->sc;
        }
        else {
            cu->scs_to_resolve[i] = cu->strings[sh_idx];
            cu->scs[i] = NULL;
        }
        if (apr_thread_mutex_unlock(tc->instance->mutex_sc_weakhash) != APR_SUCCESS)
            MVM_exception_throw_adhoc(tc, "Unable to unlock SC weakhash");
    }
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

    /* Allocate outer fixup list for frames. */
    rs->frame_outer_fixups = malloc(sizeof(MVMuint16) * rs->expected_frames);

    /* Load frames. */
    pos = rs->frame_seg;
    for (i = 0; i < rs->expected_frames; i++) {
        /* Ensure we can read a frame here. */
        ensure_can_read(tc, cu, rs, pos, FRAME_HEADER_SIZE);

        /* Allocate frame and get/check bytecode start/length. */
        frames[i] = malloc(sizeof(MVMStaticFrame));
        memset(frames[i], 0, sizeof(MVMStaticFrame));
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

        /* Get compilation unit unique ID and name. */
        frames[i]->cuuid = get_heap_string(tc, cu, rs, pos, 16);
        frames[i]->name  = get_heap_string(tc, cu, rs, pos, 18);

        /* Add frame outer fixup to fixup list. */
        rs->frame_outer_fixups[i] = read_int16(pos, 20);

        /* Get annotations details */
        {
            MVMuint32 annot_offset = read_int32(pos, 22);
            MVMuint32 num_annotations = read_int32(pos, 26);
            if (annot_offset + num_annotations * 10 > rs->annotation_size) {
                cleanup_all(tc, rs);
                MVM_exception_throw_adhoc(tc, "Frame annotation segment overflows bytecode stream");
            }
            frames[i]->annotations = rs->annotation_seg + annot_offset;
            frames[i]->num_annotations = num_annotations;
        }

        /* Read number of handlers. */
        frames[i]->num_handlers = read_int32(pos, 30);

        pos += FRAME_HEADER_SIZE;

        /* Read the local types. */
        if (frames[i]->num_locals) {
            ensure_can_read(tc, cu, rs, pos, 2 * frames[i]->num_locals);
            frames[i]->local_types = malloc(sizeof(MVMuint16) * frames[i]->num_locals);
            for (j = 0; j < frames[i]->num_locals; j++)
                frames[i]->local_types[j] = read_int16(pos, 2 * j);
            pos += 2 * frames[i]->num_locals;
        }

        /* Read the lexical types. */
        if (frames[i]->num_lexicals) {
            /* Allocate names hash and types list. */
            /* lexical_names must start out as null, but it's already zeroed. */
            /* frames[i]->lexical_names = NULL; */
            frames[i]->lexical_types = malloc(sizeof(MVMuint16) * frames[i]->num_lexicals);

            /* Read in data. */
            ensure_can_read(tc, cu, rs, pos, 4 * frames[i]->num_lexicals);
            for (j = 0; j < frames[i]->num_lexicals; j++) {
                MVMString *name = get_heap_string(tc, cu, rs, pos, 4 * j + 2);
                MVMLexicalHashEntry *entry = calloc(sizeof(MVMLexicalHashEntry), 1);
                entry->value = j;

                frames[i]->lexical_types[j] = read_int16(pos, 4 * j);
                MVM_string_flatten(tc, name);
                MVM_HASH_BIND(tc, frames[i]->lexical_names, name, entry)
            }
            pos += 4 * frames[i]->num_lexicals;
        }

        /* Read in handlers. */
        if (frames[i]->num_handlers) {
            /* Allocate space for handler data. */
            frames[i]->handlers = malloc(frames[i]->num_handlers * sizeof(MVMFrameHandler));

            /* Read each handler. */
            ensure_can_read(tc, cu, rs, pos, frames[i]->num_handlers * FRAME_HANDLER_SIZE);
            for (j = 0; j < frames[i]->num_handlers; j++) {
                frames[i]->handlers[j].start_offset = read_int32(pos, 0);
                frames[i]->handlers[j].end_offset = read_int32(pos, 4);
                frames[i]->handlers[j].category_mask = read_int32(pos, 8);
                frames[i]->handlers[j].action = read_int16(pos, 12);
                frames[i]->handlers[j].block_reg = read_int16(pos, 14);
                frames[i]->handlers[j].goto_offset = read_int32(pos, 16);
                pos += FRAME_HANDLER_SIZE;
            }
        }

        /* Associate frame with compilation unit. */
        frames[i]->cu = cu;

        /* Allocate default lexical environment storage. */
        frames[i]->env_size = frames[i]->num_lexicals * sizeof(MVMRegister);
        frames[i]->static_env = malloc(frames[i]->env_size);
        memset(frames[i]->static_env, 0, frames[i]->env_size);
    }

    /* Fixup outers. */
    for (i = 0; i < rs->expected_frames; i++) {
        if (rs->frame_outer_fixups[i] != i) {
            if (rs->frame_outer_fixups[i] < rs->expected_frames) {
                frames[i]->outer = frames[rs->frame_outer_fixups[i]];
            }
            else {
                cleanup_all(tc, rs);
                MVM_exception_throw_adhoc(tc, "Invalid frame outer index; cannot fixup");
            }
        }
    }

    return frames;
}

/* Loads the callsites. */
static MVMCallsite ** deserialize_callsites(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMCallsite **callsites;
    MVMuint8     *pos;
    MVMuint32     i, j, elems, positionals, nameds;

    /* Allocate space for callsites. */
    if (rs->expected_callsites == 0)
        return NULL;
    callsites = malloc(sizeof(MVMCallsite *) * rs->expected_callsites);

    /* Load callsites. */
    pos = rs->callsite_seg;
    for (i = 0; i < rs->expected_callsites; i++) {
        MVMuint8 has_flattening = 0;
        positionals = 0;
        nameds = 0;

        /* Ensure we can read at least an element count. */
        ensure_can_read(tc, cu, rs, pos, 2);
        elems = read_int16(pos, 0);
        pos += 2;

        /* Allocate space for the callsite. */
        callsites[i] = malloc(sizeof(MVMCallsite));
        if (elems)
            callsites[i]->arg_flags = malloc(elems);

        /* Ensure we can read in a callsite of this size, and do so. */
        ensure_can_read(tc, cu, rs, pos, elems);
        for (j = 0; j < elems; j++)
            callsites[i]->arg_flags[j] = read_int8(pos, j);
        pos += elems;

        /* Add alignment. */
        pos += elems % 2;

        /* Count positional arguments. */
        /* Validate that all positionals come before all nameds. */
        for (j = 0; j < elems; j++) {
            if (callsites[i]->arg_flags[j] & (MVM_CALLSITE_ARG_FLAT | MVM_CALLSITE_ARG_FLAT_NAMED)) {
                if (!(callsites[i]->arg_flags[j] & MVM_CALLSITE_ARG_OBJ)) {
                    MVM_exception_throw_adhoc(tc, "Flattened args must be objects");
                }
                if (nameds) {
                    MVM_exception_throw_adhoc(tc, "All positional args must appear first");
                }
                has_flattening = 1;
                positionals++;
            }
            else if (callsites[i]->arg_flags[j] & MVM_CALLSITE_ARG_NAMED) {
                nameds += 2;
            }
            else if (nameds) { /* positional appearing after a named one */
                MVM_exception_throw_adhoc(tc, "All positional args must appear first");
            }
            else positionals++;
        }
        callsites[i]->num_pos   = positionals;
        callsites[i]->arg_count = positionals + nameds;
        callsites[i]->has_flattening = has_flattening;

        /* Track maximum callsite size we've seen. (Used for now, though
         * in the end we probably should calculate it by frame.) */
        if (positionals + nameds > cu->max_callsite_size)
            cu->max_callsite_size = positionals + nameds;
    }

    return callsites;
}

/* Creates code objects to go with each of the static frames. */
static void create_code_objects(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32  i;
    MVMObject *code_type;

    cu->coderefs = malloc(sizeof(MVMObject *) * cu->num_frames);
    memset(cu->coderefs, 0, sizeof(MVMObject *) * cu->num_frames);

    code_type = tc->instance->boot_types->BOOTCode;
    for (i = 0; i < cu->num_frames; i++) {
        cu->coderefs[i] = REPR(code_type)->allocate(tc, STABLE(code_type));
        ((MVMCode *)cu->coderefs[i])->body.sf = cu->frames[i];
    }
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

    /* Load SC dependencies. */
    deserialize_sc_deps(tc, cu, rs);

    /* Load the static frame info and give each one a code reference. */
    cu->frames = deserialize_frames(tc, cu, rs);
    cu->num_frames = rs->expected_frames;
    create_code_objects(tc, cu);

    /* Load callsites. */
    cu->max_callsite_size = 0;
    cu->callsites = deserialize_callsites(tc, cu, rs);
    cu->num_callsites = rs->expected_callsites;

    /* Resolve HLL name. */
    cu->hll_name = cu->strings[rs->hll_str_idx];
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&cu->hll_name);

    /* Resolve special frames. */
    if (rs->main_frame)
        cu->main_frame = cu->frames[rs->main_frame - 1];
    if (rs->load_frame)
        cu->load_frame = cu->frames[rs->load_frame - 1];
    if (rs->deserialize_frame)
        cu->deserialize_frame = cu->frames[rs->deserialize_frame - 1];

    /* Clean up reader state. */
    cleanup_all(tc, rs);
}
