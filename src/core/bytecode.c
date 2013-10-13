#include "moar.h"

/* Some constants. */
#define HEADER_SIZE             88
#define MIN_BYTECODE_VERSION    1
#define MAX_BYTECODE_VERSION    1
#define FRAME_HEADER_SIZE       (7 * 4 + 3 * 2)
#define FRAME_HANDLER_SIZE      (4 * 4 + 2 * 2)

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
    MVMCompUnitBody *cu_body = &cu->body;
    if (pos + size > cu_body->data_start + cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Read past end of bytecode stream");
    }
}

/* Reads a string index, looks up the string and returns it. Bounds
 * checks the string heap index too. */
static MVMString * get_heap_string(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, char *buffer, size_t offset) {
    MVMCompUnitBody *cu_body = &cu->body;
    MVMuint16 heap_index = read_int16(buffer, offset);
    if (heap_index >= cu_body->num_strings) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "String heap index beyond end of string heap");
    }
    return cu_body->strings[heap_index];
}

/* Dissects the bytecode stream and hands back a reader pointing to the
 * various parts of it. */
static ReaderState * dissect_bytecode(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMCompUnitBody *cu_body = &cu->body;
    ReaderState *rs = NULL;
    MVMuint32 version, offset, size;

    /* Sanity checks. */
    if (cu_body->data_size < HEADER_SIZE)
        MVM_exception_throw_adhoc(tc, "Bytecode stream shorter than header");
    if (memcmp(cu_body->data_start, "MOARVM\r\n", 8) != 0)
        MVM_exception_throw_adhoc(tc, "Bytecode stream corrupt (missing magic string)");
    version = read_int32(cu_body->data_start, 8);
    if (version < MIN_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too low");
    if (version > MAX_BYTECODE_VERSION)
        MVM_exception_throw_adhoc(tc, "Bytecode stream version too high");

    /* Allocate reader state. */
    rs = malloc(sizeof(ReaderState));
    memset(rs, 0, sizeof(ReaderState));
    rs->version = version;

    /* Locate SC dependencies segment. */
    offset = read_int32(cu_body->data_start, 12);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Serialization contexts segment starts after end of stream");
    }
    rs->sc_seg       = cu_body->data_start + offset;
    rs->expected_scs = read_int32(cu_body->data_start, 16);

    /* Locate frames segment. */
    offset = read_int32(cu_body->data_start, 20);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Frames segment starts after end of stream");
    }
    rs->frame_seg       = cu_body->data_start + offset;
    rs->expected_frames = read_int32(cu_body->data_start, 24);

    /* Locate callsites segment. */
    offset = read_int32(cu_body->data_start, 28);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Callsites segment starts after end of stream");
    }
    rs->callsite_seg       = cu_body->data_start + offset;
    rs->expected_callsites = read_int32(cu_body->data_start, 32);

    /* Locate strings segment. */
    offset = read_int32(cu_body->data_start, 40);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Strings segment starts after end of stream");
    }
    rs->string_seg       = cu_body->data_start + offset;
    rs->expected_strings = read_int32(cu_body->data_start, 44);

    /* Locate bytecode segment. */
    offset = read_int32(cu_body->data_start, 56);
    size = read_int32(cu_body->data_start, 60);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu_body->data_start + offset;
    rs->bytecode_size = size;

    /* Locate annotations segment. */
    offset = read_int32(cu_body->data_start, 64);
    size = read_int32(cu_body->data_start, 68);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Annotation segment overflows end of stream");
    }
    rs->annotation_seg  = cu_body->data_start + offset;
    rs->annotation_size = size;

    /* Locate HLL name */
    rs->hll_str_idx = read_int32(cu_body->data_start, 72);

    /* Locate special frame indexes. Note, they are 0 for none, and the
     * index + 1 if there is one. */
    rs->main_frame = read_int32(cu_body->data_start, 76);
    rs->load_frame = read_int32(cu_body->data_start, 80);
    rs->deserialize_frame = read_int32(cu_body->data_start, 84);
    if (rs->main_frame > rs->expected_frames
            || rs->load_frame > rs->expected_frames
            || rs->deserialize_frame > rs->expected_frames) {
        MVM_exception_throw_adhoc(tc, "Special frame index out of bounds");
    }

    return rs;
}

/* Loads the string heap. */
static MVMString ** deserialize_strings(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMCompUnitBody *cu_body = &cu->body;
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
        MVM_ASSIGN_REF(tc, cu, strings[i], MVM_string_utf8_decode(tc, tc->instance->VMString, pos, ss));
        pos += ss;

        /* Add alignment. */
        pos += ss & 3 ? 4 - (ss & 3) : 0;
    }

    return strings;
}

/* Loads the SC dependencies list. */
static void deserialize_sc_deps(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMCompUnitBody *cu_body = &cu->body;
    MVMuint32 i, sh_idx;
    MVMuint8  *pos;

    /* Allocate SC lists in compilation unit. */
    cu_body->scs = malloc(rs->expected_scs * sizeof(MVMSerializationContext *));
    cu_body->scs_to_resolve = malloc(rs->expected_scs * sizeof(MVMSerializationContextBody *));
    cu_body->num_scs = rs->expected_scs;

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
        if (sh_idx >= cu_body->num_strings) {
            cleanup_all(tc, rs);
            MVM_exception_throw_adhoc(tc, "String heap index beyond end of string heap");
        }
        handle = cu_body->strings[sh_idx];

        /* See if we can resolve it. */
        uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
        MVM_string_flatten(tc, handle);
        MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
        if (scb && scb->sc) {
            cu_body->scs_to_resolve[i] = NULL;
            MVM_ASSIGN_REF(tc, cu, cu_body->scs[i], scb->sc);
        }
        else {
            if (!scb) {
                scb = calloc(1, sizeof(MVMSerializationContextBody));
                scb->handle = handle;
                MVM_HASH_BIND(tc, tc->instance->sc_weakhash, handle, scb);
            }
            cu_body->scs_to_resolve[i] = scb;
            cu_body->scs[i] = NULL;
        }
        uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);
    }
}

/* Loads the static frame information (what locals we have, bytecode offset,
 * lexicals, etc.) */
static MVMStaticFrame ** deserialize_frames(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMCompUnitBody *cu_body = &cu->body;
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
        MVMStaticFrame *static_frame;
        MVMStaticFrameBody *static_frame_body;
        /* Ensure we can read a frame here. */
        ensure_can_read(tc, cu, rs, pos, FRAME_HEADER_SIZE);

        /* Allocate frame and get/check bytecode start/length. */
        static_frame = (MVMStaticFrame *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStaticFrame);
        MVM_ASSIGN_REF(tc, cu, frames[i], static_frame);
        static_frame_body = &static_frame->body;

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
        static_frame_body->bytecode = rs->bytecode_seg + bytecode_pos;
        static_frame_body->bytecode_size = bytecode_size;

        /* Get number of locals and lexicals. */
        static_frame_body->num_locals = read_int32(pos, 8);
        static_frame_body->num_lexicals = read_int32(pos, 12);

        /* Get compilation unit unique ID and name. */
        MVM_ASSIGN_REF(tc, static_frame, static_frame_body->cuuid, get_heap_string(tc, cu, rs, pos, 16));
        MVM_ASSIGN_REF(tc, static_frame, static_frame_body->name, get_heap_string(tc, cu, rs, pos, 18));

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
            static_frame_body->annotations_data = rs->annotation_seg + annot_offset;
            static_frame_body->num_annotations = num_annotations;
        }

        /* Read number of handlers. */
        static_frame_body->num_handlers = read_int32(pos, 30);

        pos += FRAME_HEADER_SIZE;

        /* Read the local types. */
        if (static_frame_body->num_locals) {
            ensure_can_read(tc, cu, rs, pos, 2 * static_frame_body->num_locals);
            static_frame_body->local_types = malloc(sizeof(MVMuint16) * static_frame_body->num_locals);
            for (j = 0; j < static_frame_body->num_locals; j++)
                static_frame_body->local_types[j] = read_int16(pos, 2 * j);
            pos += 2 * static_frame_body->num_locals;
        }

        /* Read the lexical types. */
        if (static_frame_body->num_lexicals) {
            /* Allocate names hash and types list. */
            static_frame_body->lexical_types = malloc(sizeof(MVMuint16) * static_frame_body->num_lexicals);

            /* Read in data. */
            ensure_can_read(tc, cu, rs, pos, 4 * static_frame_body->num_lexicals);
            if (static_frame_body->num_lexicals) {
                static_frame_body->lexical_names_list = malloc(sizeof(MVMLexicalRegistry *) * static_frame_body->num_lexicals);
            }
            for (j = 0; j < static_frame_body->num_lexicals; j++) {
                MVMString *name = get_heap_string(tc, cu, rs, pos, 4 * j + 2);
                MVMLexicalRegistry *entry = calloc(1, sizeof(MVMLexicalRegistry));

                MVM_ASSIGN_REF(tc, static_frame, entry->key, name);
                static_frame_body->lexical_names_list[j] = entry;
                entry->value = j;

                static_frame_body->lexical_types[j] = read_int16(pos, 4 * j);
                MVM_string_flatten(tc, name);
                MVM_HASH_BIND(tc, static_frame_body->lexical_names, name, entry)
            }
            pos += 4 * static_frame_body->num_lexicals;
        }

        /* Read in handlers. */
        if (static_frame_body->num_handlers) {
            /* Allocate space for handler data. */
            static_frame_body->handlers = malloc(static_frame_body->num_handlers * sizeof(MVMFrameHandler));

            /* Read each handler. */
            ensure_can_read(tc, cu, rs, pos, static_frame_body->num_handlers * FRAME_HANDLER_SIZE);
            for (j = 0; j < static_frame_body->num_handlers; j++) {
                static_frame_body->handlers[j].start_offset = read_int32(pos, 0);
                static_frame_body->handlers[j].end_offset = read_int32(pos, 4);
                static_frame_body->handlers[j].category_mask = read_int32(pos, 8);
                static_frame_body->handlers[j].action = read_int16(pos, 12);
                static_frame_body->handlers[j].block_reg = read_int16(pos, 14);
                static_frame_body->handlers[j].goto_offset = read_int32(pos, 16);
                pos += FRAME_HANDLER_SIZE;
            }
        }

        /* Associate frame with compilation unit. */
        MVM_ASSIGN_REF(tc, static_frame, static_frame_body->cu, cu);

        /* Allocate default lexical environment storage. */
        static_frame_body->env_size = static_frame_body->num_lexicals * sizeof(MVMRegister);
        static_frame_body->static_env = calloc(1, static_frame_body->env_size);
    }

    /* Fixup outers. */
    for (i = 0; i < rs->expected_frames; i++) {
        if (rs->frame_outer_fixups[i] != i) {
            if (rs->frame_outer_fixups[i] < rs->expected_frames) {
                MVM_ASSIGN_REF(tc, frames[i], frames[i]->body.outer, frames[rs->frame_outer_fixups[i]]);
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
    MVMCompUnitBody *cu_body = &cu->body;

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
        if (positionals + nameds > cu_body->max_callsite_size)
            cu_body->max_callsite_size = positionals + nameds;
    }

    return callsites;
}

/* Creates code objects to go with each of the static frames. */
static void create_code_objects(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32  i;
    MVMObject *code_type;
    MVMCompUnitBody *cu_body = &cu->body;

    cu_body->coderefs = malloc(cu_body->num_frames * sizeof(MVMObject *));

    code_type = tc->instance->boot_types.BOOTCode;
    for (i = 0; i < cu_body->num_frames; i++) {
        MVMCode *coderef = (MVMCode *)REPR(code_type)->allocate(tc, STABLE(code_type));
        MVM_ASSIGN_REF(tc, cu, cu_body->coderefs[i], coderef);
        MVM_ASSIGN_REF(tc, coderef, coderef->body.sf, cu_body->frames[i]);
        MVM_ASSIGN_REF(tc, cu_body->frames[i], cu_body->frames[i]->body.static_code, coderef);
    }
}

/* Takes a compilation unit pointing at a bytecode stream (which actually
 * has more than just the executive bytecode, but also various declarations,
 * like frames). Unpacks it and populates the compilation unit. */
void MVM_bytecode_unpack(MVMThreadContext *tc, MVMCompUnit *cu) {
    ReaderState *rs;
    MVMCompUnitBody *cu_body = &cu->body;
    /* Allocate directly in generation 2 so the object is not moving around. */
    MVM_gc_allocate_gen2_default_set(tc);

    /* Dissect the bytecode into its parts. */
    rs = dissect_bytecode(tc, cu);

    /* Load the strings heap. */
    cu_body->strings = deserialize_strings(tc, cu, rs);
    cu_body->num_strings = rs->expected_strings;

    /* Load SC dependencies. */
    deserialize_sc_deps(tc, cu, rs);

    /* Load the static frame info and give each one a code reference. */
    cu_body->frames = deserialize_frames(tc, cu, rs);
    cu_body->num_frames = rs->expected_frames;
    create_code_objects(tc, cu);

    /* Load callsites. */
    cu_body->max_callsite_size = 0;
    cu_body->callsites = deserialize_callsites(tc, cu, rs);
    cu_body->num_callsites = rs->expected_callsites;

    /* Resolve HLL name. */
    MVM_ASSIGN_REF(tc, cu, cu_body->hll_name, cu_body->strings[rs->hll_str_idx]);

    /* Resolve special frames. */
    if (rs->main_frame)
        MVM_ASSIGN_REF(tc, cu, cu_body->main_frame, cu_body->frames[rs->main_frame - 1]);
    if (rs->load_frame)
        MVM_ASSIGN_REF(tc, cu, cu_body->load_frame, cu_body->frames[rs->load_frame - 1]);
    if (rs->deserialize_frame)
        MVM_ASSIGN_REF(tc, cu, cu_body->deserialize_frame, cu_body->frames[rs->deserialize_frame - 1]);

    /* Clean up reader state. */
    cleanup_all(tc, rs);

    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* returns the annotation for that bytecode offset */
MVMBytecodeAnnotation * MVM_bytecode_resolve_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMuint32 offset) {
    MVMBytecodeAnnotation *ba = NULL;
    MVMuint32 i, j;

    if (offset >= 0 && offset < sfb->bytecode_size) {
        MVMint8 *cur_anno = sfb->annotations_data;
        for (i = 0; i < sfb->num_annotations; i++) {
            MVMint32 ann_offset = read_int32(cur_anno, 0);
            if (ann_offset > offset)
                break;
            cur_anno += 10;
        }
        if (i == sfb->num_annotations)
            cur_anno -= 10;
        if (i > 0) {
            ba = malloc(sizeof(MVMBytecodeAnnotation));
            ba->bytecode_offset = read_int32(cur_anno, 0);
            ba->filename_string_heap_index = read_int16(cur_anno, 4);
            ba->line_number = read_int32(cur_anno, 6);
        }
    }

    return ba;
}
