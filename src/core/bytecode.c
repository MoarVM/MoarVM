#include "moar.h"

/* Some constants. */
#define HEADER_SIZE                 92
#define MIN_BYTECODE_VERSION        2
#define MAX_BYTECODE_VERSION        5
#define FRAME_HEADER_SIZE           ((cu->body.bytecode_version >= 4 ? 11 : 9) * 4 + (cu->body.bytecode_version >= 4 ? 3 : 2) * 2)
#define FRAME_HANDLER_SIZE          (4 * 4 + 2 * 2)
#define FRAME_SLV_SIZE              (2 * 2 + 2 * 4)
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

/* Frame flags. */
#define FRAME_FLAG_EXIT_HANDLER     1
#define FRAME_FLAG_IS_THUNK         2

/* Describes the current reader state. */
typedef struct {
    /* General info. */
    MVMuint32 version;

    /* The string heap. */
    MVMuint8  *string_seg;
    MVMuint32  expected_strings;

    /* The SC dependencies segment. */
    MVMuint32  expected_scs;
    MVMuint8  *sc_seg;

    /* The extension ops segment. */
    MVMuint8 *extop_seg;
    MVMuint32 expected_extops;

    /* The frame segment. */
    MVMuint32  expected_frames;
    MVMuint8  *frame_seg;
    MVMuint16 *frame_outer_fixups;

    /* The callsites segment. */
    MVMuint8  *callsite_seg;
    MVMuint32  expected_callsites;

    /* The bytecode segment. */
    MVMuint32  bytecode_size;
    MVMuint8  *bytecode_seg;

    /* The annotations segment */
    MVMuint8  *annotation_seg;
    MVMuint32  annotation_size;

    /* HLL name string index */
    MVMuint32  hll_str_idx;

    /* The limit we can not read beyond. */
    MVMuint8 *read_limit;

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

/* Cleans up reader state. */
static void cleanup_all(MVMThreadContext *tc, ReaderState *rs) {
    if (rs->frame_outer_fixups) {
        MVM_free(rs->frame_outer_fixups);
        rs->frame_outer_fixups = NULL;
    }
    MVM_free(rs);
}

/* Ensures we can read a certain amount of bytes without overrunning the end
 * of the stream. */
MVM_STATIC_INLINE void ensure_can_read(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, MVMuint8 *pos, MVMuint32 size) {
    if (pos + size > rs->read_limit) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Read past end of bytecode stream");
    }
}

/* Reads a string index, looks up the string and returns it. Bounds
 * checks the string heap index too. */
static MVMString * get_heap_string(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs, MVMuint8 *buffer, size_t offset) {
    MVMCompUnitBody *cu_body = &cu->body;
    MVMuint32 heap_index = read_int32(buffer, offset);
    if (heap_index >= cu_body->num_strings) {
        if (rs)
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
    rs = MVM_malloc(sizeof(ReaderState));
    memset(rs, 0, sizeof(ReaderState));
    rs->version = version;
    rs->read_limit = cu_body->data_start + cu_body->data_size;
    cu->body.bytecode_version = version;

    /* Locate SC dependencies segment. */
    offset = read_int32(cu_body->data_start, SCDEP_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Serialization contexts segment starts after end of stream");
    }
    rs->sc_seg       = cu_body->data_start + offset;
    rs->expected_scs = read_int32(cu_body->data_start, SCDEP_HEADER_OFFSET + 4);

    /* Locate extension ops segment. */
    offset = read_int32(cu_body->data_start, EXTOP_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Extension ops segment starts after end of stream");
    }
    rs->extop_seg       = cu_body->data_start + offset;
    rs->expected_extops = read_int32(cu_body->data_start, EXTOP_HEADER_OFFSET + 4);

    /* Locate frames segment. */
    offset = read_int32(cu_body->data_start, FRAME_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Frames segment starts after end of stream");
    }
    rs->frame_seg       = cu_body->data_start + offset;
    rs->expected_frames = read_int32(cu_body->data_start, FRAME_HEADER_OFFSET + 4);

    /* Locate callsites segment. */
    offset = read_int32(cu_body->data_start, CALLSITE_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Callsites segment starts after end of stream");
    }
    rs->callsite_seg       = cu_body->data_start + offset;
    rs->expected_callsites = read_int32(cu_body->data_start, CALLSITE_HEADER_OFFSET + 4);

    /* Locate strings segment. */
    offset = read_int32(cu_body->data_start, STRING_HEADER_OFFSET);
    if (offset > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Strings segment starts after end of stream");
    }
    rs->string_seg       = cu_body->data_start + offset;
    rs->expected_strings = read_int32(cu_body->data_start, STRING_HEADER_OFFSET + 4);

    /* Get SC data, if any. */
    offset = read_int32(cu_body->data_start, SCDATA_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, SCDATA_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Serialized data segment overflows end of stream");
    }
    if (offset) {
        cu_body->serialized = cu_body->data_start + offset;
        cu_body->serialized_size = size;
    }

    /* Locate bytecode segment. */
    offset = read_int32(cu_body->data_start, BYTECODE_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, BYTECODE_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode segment overflows end of stream");
    }
    rs->bytecode_seg  = cu_body->data_start + offset;
    rs->bytecode_size = size;

    /* Locate annotations segment. */
    offset = read_int32(cu_body->data_start, ANNOTATION_HEADER_OFFSET);
    size = read_int32(cu_body->data_start, ANNOTATION_HEADER_OFFSET + 4);
    if (offset > cu_body->data_size || offset + size > cu_body->data_size) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Annotation segment overflows end of stream");
    }
    rs->annotation_seg  = cu_body->data_start + offset;
    rs->annotation_size = size;

    /* Locate HLL name */
    rs->hll_str_idx = read_int32(cu_body->data_start, HLL_NAME_HEADER_OFFSET);

    /* Locate special frame indexes. Note, they are 0 for none, and the
     * index + 1 if there is one. */
    rs->main_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET);
    rs->load_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET + 4);
    rs->deserialize_frame = read_int32(cu_body->data_start, SPECIAL_FRAME_HEADER_OFFSET + 8);
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
    strings = MVM_malloc(sizeof(MVMString *) * rs->expected_strings);

    /* Load strings. */
    pos = rs->string_seg;
    for (i = 0; i < rs->expected_strings; i++) {
        MVMint32 decode_utf8 = 1;

        /* Ensure we can read at least a string size here and do so. */
        ensure_can_read(tc, cu, rs, pos, 4);
        ss = read_int32(pos, 0);
        pos += 4;

        /* At high enough bytecode file versions, the LSB on this number
         * is 1 if we should decode as UTF-8, and 0 if Latin-1 will do. */
        if (rs->version >= 5) {
            decode_utf8 = ss & 1;
            ss = ss >> 1;
        }

        /* Ensure we can read in the string of this size, and decode
         * it if so. */
        ensure_can_read(tc, cu, rs, pos, ss);
        MVM_ASSIGN_REF(tc, &(cu->common.header), strings[i], decode_utf8
            ? MVM_string_utf8_decode(tc, tc->instance->VMString, (char *)pos, ss)
            : MVM_string_latin1_decode(tc, tc->instance->VMString, (char *)pos, ss));
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
    cu_body->scs = MVM_malloc(rs->expected_scs * sizeof(MVMSerializationContext *));
    cu_body->scs_to_resolve = MVM_malloc(rs->expected_scs * sizeof(MVMSerializationContextBody *));
    cu_body->sc_handle_idxs = MVM_malloc(rs->expected_scs * sizeof(MVMint32));
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
        cu_body->sc_handle_idxs[i] = sh_idx;
        handle = cu_body->strings[sh_idx];

        /* See if we can resolve it. */
        uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
        MVM_string_flatten(tc, handle);
        MVM_HASH_GET(tc, tc->instance->sc_weakhash, handle, scb);
        if (scb && scb->sc) {
            cu_body->scs_to_resolve[i] = NULL;
            MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->scs[i], scb->sc);
        }
        else {
            if (!scb) {
                scb = MVM_calloc(1, sizeof(MVMSerializationContextBody));
                scb->handle = handle;
                MVM_HASH_BIND(tc, tc->instance->sc_weakhash, handle, scb);
                MVM_sc_add_all_scs_entry(tc, scb);
            }
            cu_body->scs_to_resolve[i] = scb;
            cu_body->scs[i] = NULL;
        }
        uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);
    }
}

/* Loads the extension op records. */
static MVMExtOpRecord * deserialize_extop_records(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMExtOpRecord *extops;
    MVMuint32 num = rs->expected_extops;
    MVMuint8 *pos;
    MVMuint32 i;

    if (num == 0)
        return NULL;

    extops = MVM_calloc(num, sizeof *extops);

    pos = rs->extop_seg;
    for (i = 0; i < num; i++) {
        MVMuint32 name_idx;
        MVMuint16 operand_bytes = 0;
        MVMuint8 *operand_descriptor = extops[i].operand_descriptor;

        /* Read name string index. */
        ensure_can_read(tc, cu, rs, pos, 4);
        name_idx = read_int32(pos, 0);
        pos += 4;

        /* Lookup name string. */
        if (name_idx >= cu->body.num_strings) {
            cleanup_all(tc, rs);
            MVM_exception_throw_adhoc(tc,
                    "String heap index beyond end of string heap");
        }
        extops[i].name = cu->body.strings[name_idx];

        /* Read operand descriptor. */
        ensure_can_read(tc, cu, rs, pos, 8);
        memcpy(operand_descriptor, pos, 8);
        pos += 8;

        /* Validate operand descriptor.
         * TODO: Unify with validation in MVM_ext_register_extop? */
        {
            MVMuint8 j = 0;

            for(; j < 8; j++) {
                MVMuint8 flags = operand_descriptor[j];

                if (!flags)
                    break;

                switch (flags & MVM_operand_rw_mask) {
                    case MVM_operand_literal:
                        goto check_literal;

                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        operand_bytes += 2;
                        goto check_reg;

                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex:
                        operand_bytes += 4;
                        goto check_reg;

                    default:
                        goto fail;
                }

            check_literal:
                switch (flags & MVM_operand_type_mask) {
                    case MVM_operand_int8:
                        operand_bytes += 1;
                        continue;

                    case MVM_operand_int16:
                        operand_bytes += 2;
                        continue;

                    case MVM_operand_int32:
                        operand_bytes += 4;
                        continue;

                    case MVM_operand_int64:
                        operand_bytes += 8;
                        continue;

                    case MVM_operand_num32:
                        operand_bytes += 4;
                        continue;

                    case MVM_operand_num64:
                        operand_bytes += 8;
                        continue;

                    case MVM_operand_str:
                        operand_bytes += 2;
                        continue;

                    case MVM_operand_coderef:
                        operand_bytes += 2;
                        continue;

                    case MVM_operand_ins:
                    case MVM_operand_callsite:
                    default:
                        goto fail;
                }

            check_reg:
                switch (flags & MVM_operand_type_mask) {
                    case MVM_operand_int8:
                    case MVM_operand_int16:
                    case MVM_operand_int32:
                    case MVM_operand_int64:
                    case MVM_operand_num32:
                    case MVM_operand_num64:
                    case MVM_operand_str:
                    case MVM_operand_obj:
                    case MVM_operand_type_var:
                        continue;

                    default:
                        goto fail;
                }

            fail:
                cleanup_all(tc, rs);
                MVM_exception_throw_adhoc(tc, "Invalid operand descriptor");
            }
        }

        extops[i].operand_bytes = operand_bytes;
    }

    return extops;
}

/* Loads the static frame information (what locals we have, bytecode offset,
 * lexicals, etc.) */
static MVMStaticFrame ** deserialize_frames(MVMThreadContext *tc, MVMCompUnit *cu, ReaderState *rs) {
    MVMStaticFrame **frames;
    MVMuint8        *pos;
    MVMuint32        bytecode_pos, bytecode_size, i, j;

    /* Allocate frames array. */
    if (rs->expected_frames == 0) {
        cleanup_all(tc, rs);
        MVM_exception_throw_adhoc(tc, "Bytecode file must have at least one frame");
    }
    frames = MVM_malloc(sizeof(MVMStaticFrame *) * rs->expected_frames);

    /* Allocate outer fixup list for frames. */
    rs->frame_outer_fixups = MVM_malloc(sizeof(MVMuint16) * rs->expected_frames);

    /* Load frames. */
    pos = rs->frame_seg;
    for (i = 0; i < rs->expected_frames; i++) {
        MVMStaticFrame *static_frame;
        MVMStaticFrameBody *static_frame_body;

        /* Ensure we can read a frame here. */
        ensure_can_read(tc, cu, rs, pos, FRAME_HEADER_SIZE);

        /* Allocate frame and get/check bytecode start/length. */
        static_frame = (MVMStaticFrame *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStaticFrame);
        MVM_ASSIGN_REF(tc, &(cu->common.header), frames[i], static_frame);
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
        static_frame_body->bytecode      = rs->bytecode_seg + bytecode_pos;
        static_frame_body->bytecode_size = bytecode_size;
        static_frame_body->orig_bytecode = static_frame_body->bytecode;

        /* Get number of locals and lexicals. */
        static_frame_body->num_locals   = read_int32(pos, 8);
        static_frame_body->num_lexicals = read_int32(pos, 12);

        /* Get compilation unit unique ID and name. */
        MVM_ASSIGN_REF(tc, &(static_frame->common.header), static_frame_body->cuuid, get_heap_string(tc, cu, rs, pos, 16));
        MVM_ASSIGN_REF(tc, &(static_frame->common.header), static_frame_body->name, get_heap_string(tc, cu, rs, pos, 20));

        /* Add frame outer fixup to fixup list. */
        rs->frame_outer_fixups[i] = read_int16(pos, 24);

        /* Get annotations details */
        {
            MVMuint32 annot_offset    = read_int32(pos, 26);
            MVMuint32 num_annotations = read_int32(pos, 30);
            if (annot_offset + num_annotations * 12 > rs->annotation_size) {
                cleanup_all(tc, rs);
                MVM_exception_throw_adhoc(tc, "Frame annotation segment overflows bytecode stream");
            }
            static_frame_body->annotations_data = rs->annotation_seg + annot_offset;
            static_frame_body->num_annotations  = num_annotations;
        }

        /* Read number of handlers. */
        static_frame_body->num_handlers = read_int32(pos, 34);

        /* Read exit handler flag (version 2 and higher). */
        if (rs->version >= 2) {
            MVMint16 flags = read_int16(pos, 38);
            static_frame_body->has_exit_handler = flags & FRAME_FLAG_EXIT_HANDLER;
            static_frame_body->is_thunk         = flags & FRAME_FLAG_IS_THUNK;
        }

        /* Read code object SC indexes (version 4 and higher). */
        if (rs->version >= 4) {
            static_frame_body->code_obj_sc_dep_idx = read_int32(pos, 42);
            static_frame_body->code_obj_sc_idx     = read_int32(pos, 46);
        }

        /* Associate frame with compilation unit. */
        MVM_ASSIGN_REF(tc, &(static_frame->common.header), static_frame_body->cu, cu);

        /* Stash position for lazy deserialization of the rest. */
        static_frame_body->frame_data_pos = pos;

        /* Skip over the rest, making sure it's readable. */
        {
            MVMuint32 skip = 2 * static_frame_body->num_locals +
                             6 * static_frame_body->num_lexicals;
            MVMuint16 slvs = cu->body.bytecode_version >= 4 ? read_int16(pos, 40) : 0;
            pos += FRAME_HEADER_SIZE;
            ensure_can_read(tc, cu, rs, pos, skip);
            pos += skip;
            for (j = 0; j < static_frame_body->num_handlers; j++) {
                ensure_can_read(tc, cu, rs, pos, FRAME_HANDLER_SIZE);
                if (read_int32(pos, 8) & MVM_EX_CAT_LABELED) {
                    pos += FRAME_HANDLER_SIZE;
                    ensure_can_read(tc, cu, rs, pos, 2);
                    pos += 2;
                }
                else {
                    pos += FRAME_HANDLER_SIZE;
                }
            }
            ensure_can_read(tc, cu, rs, pos, slvs * FRAME_SLV_SIZE);
            pos += slvs * FRAME_SLV_SIZE;
        }
    }

    /* Fixup outers. */
    for (i = 0; i < rs->expected_frames; i++) {
        if (rs->frame_outer_fixups[i] != i) {
            if (rs->frame_outer_fixups[i] < rs->expected_frames) {
                MVM_ASSIGN_REF(tc, &(frames[i]->common.header), frames[i]->body.outer, frames[rs->frame_outer_fixups[i]]);
            }
            else {
                cleanup_all(tc, rs);
                MVM_exception_throw_adhoc(tc, "Invalid frame outer index; cannot fixup");
            }
        }
    }

    return frames;
}

/* Finishes up reading and exploding of a frame. */
void MVM_bytecode_finish_frame(MVMThreadContext *tc, MVMCompUnit *cu,
                               MVMStaticFrame *sf, MVMint32 dump_only) {
    MVMuint32 j;
    MVMuint8 *pos;
    MVMuint16 slvs;

    /* Ensure we've not already done this. */
    if (sf->body.fully_deserialized)
        return;

    /* Acquire the update mutex on the CompUnit. */
    MVM_reentrantmutex_lock(tc, (MVMReentrantMutex *)cu->body.update_mutex);

    /* Ensure no other thread has done this for us in the mean time. */
    if (sf->body.fully_deserialized) {
        MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.update_mutex);
        return;
    }

    /* Locate start of frame body. */
    pos = sf->body.frame_data_pos;

    /* Get the number of static lex values we'll need to apply. */
    slvs = cu->body.bytecode_version >= 4 ? read_int16(pos, 40) : 0;

    /* Skip past header. */
    pos += FRAME_HEADER_SIZE;

    /* Read the local types. */
    if (sf->body.num_locals) {
        sf->body.local_types = MVM_malloc(sizeof(MVMuint16) * sf->body.num_locals);
        for (j = 0; j < sf->body.num_locals; j++)
            sf->body.local_types[j] = read_int16(pos, 2 * j);
        pos += 2 * sf->body.num_locals;
    }

    /* Read the lexical types. */
    if (sf->body.num_lexicals) {
        /* Allocate names hash and types list. */
        sf->body.lexical_types = MVM_malloc(sizeof(MVMuint16) * sf->body.num_lexicals);

        /* Read in data. */
        if (sf->body.num_lexicals) {
            sf->body.lexical_names_list = MVM_malloc(sizeof(MVMLexicalRegistry *) * sf->body.num_lexicals);
        }
        for (j = 0; j < sf->body.num_lexicals; j++) {
            MVMString *name = get_heap_string(tc, cu, NULL, pos, 6 * j + 2);
            MVMLexicalRegistry *entry = MVM_calloc(1, sizeof(MVMLexicalRegistry));

            MVM_ASSIGN_REF(tc, &(sf->common.header), entry->key, name);
            sf->body.lexical_names_list[j] = entry;
            entry->value = j;

            sf->body.lexical_types[j] = read_int16(pos, 6 * j);
            MVM_string_flatten(tc, name);
            MVM_HASH_BIND(tc, sf->body.lexical_names, name, entry)
        }
        pos += 6 * sf->body.num_lexicals;
    }

    /* Read in handlers. */
    if (sf->body.num_handlers) {
        /* Allocate space for handler data. */
        sf->body.handlers = MVM_malloc(sf->body.num_handlers * sizeof(MVMFrameHandler));

        /* Read each handler. */
        for (j = 0; j < sf->body.num_handlers; j++) {
            sf->body.handlers[j].start_offset  = read_int32(pos, 0);
            sf->body.handlers[j].end_offset    = read_int32(pos, 4);
            sf->body.handlers[j].category_mask = read_int32(pos, 8);
            sf->body.handlers[j].action        = read_int16(pos, 12);
            sf->body.handlers[j].block_reg     = read_int16(pos, 14);
            sf->body.handlers[j].goto_offset   = read_int32(pos, 16);
            pos += FRAME_HANDLER_SIZE;
            if (sf->body.handlers[j].category_mask & MVM_EX_CAT_LABELED) {
                sf->body.handlers[j].label_reg = read_int16(pos, 0);
                pos += 2;
            }
        }
    }

    /* Allocate default lexical environment storage. */
    sf->body.env_size         = sf->body.num_lexicals * sizeof(MVMRegister);
    sf->body.static_env       = MVM_calloc(1, sf->body.env_size);
    sf->body.static_env_flags = MVM_calloc(1, sf->body.num_lexicals);

    /* Stash static lexical segment offset, so we can easily locate it to
     * resolve them later. */
    sf->body.frame_static_lex_pos = slvs ? pos : NULL;

    /* Read in static lexical flags. */
    for (j = 0; j < slvs; j++) {
        MVMuint16 lex_idx = read_int16(pos, 0);
        MVMuint16 flags   = read_int16(pos, 2);
        sf->body.static_env_flags[lex_idx] = flags;
        if (flags == 2 && !dump_only) {
            /* State variable; need to resolve wval immediately. Other kinds
             * can wait. */
            MVMSerializationContext *sc = MVM_sc_get_sc(tc, cu, read_int32(pos, 4));
            if (sc == NULL)
                MVM_exception_throw_adhoc(tc, "SC not yet resolved; lookup failed");
            MVM_ASSIGN_REF(tc, &(sf->common.header), sf->body.static_env[lex_idx].o,
                MVM_sc_get_object(tc, sc, read_int32(pos, 8)));
        }
        pos += FRAME_SLV_SIZE;
    }

    /* Mark the frame fully deserialized. */
    sf->body.fully_deserialized = 1;

    /* Release the update mutex again */
    MVM_reentrantmutex_unlock(tc, (MVMReentrantMutex *)cu->body.update_mutex);
}

/* Gets the SC reference for a given static lexical var for
 * vivification purposes */
MVMuint8 MVM_bytecode_find_static_lexical_scref(MVMThreadContext *tc, MVMCompUnit *cu, MVMStaticFrame *sf, MVMuint16 index, MVMint32 *sc, MVMint32 *id) {
    MVMuint16 slvs, i;

    MVMuint8 *pos = sf->body.frame_static_lex_pos;
    if (!pos)
        return 0;

    slvs = read_int16(sf->body.frame_data_pos, 40);
    for (i = 0; i < slvs; i++) {
        if (read_int16(pos, 0) == index) {
            *sc = read_int32(pos, 4);
            *id = read_int32(pos, 8);
            return 1;
        }
        pos += FRAME_SLV_SIZE;
    }

    return 0;
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
    callsites = MVM_malloc(sizeof(MVMCallsite *) * rs->expected_callsites);

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
        callsites[i] = MVM_malloc(sizeof(MVMCallsite));
        if (elems)
            callsites[i]->arg_flags = MVM_malloc(elems);

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
        callsites[i]->num_pos        = positionals;
        callsites[i]->arg_count      = positionals + nameds;
        callsites[i]->has_flattening = has_flattening;
        callsites[i]->is_interned    = 0;
        callsites[i]->with_invocant  = NULL;

        if (rs->version >= 3 && nameds) {
            ensure_can_read(tc, cu, rs, pos, (nameds / 2) * 4);
            callsites[i]->arg_names = MVM_malloc((nameds / 2) * sizeof(MVMString));
            for (j = 0; j < nameds / 2; j++) {
                callsites[i]->arg_names[j] = get_heap_string(tc, cu, rs, pos, 0);
                pos += 4;
            }
        } else {
            callsites[i]->arg_names = NULL;
        }

        /* Track maximum callsite size we've seen. (Used for now, though
         * in the end we probably should calculate it by frame.) */
        if (positionals + nameds > cu_body->max_callsite_size)
            cu_body->max_callsite_size = positionals + nameds;

        /* Try to intern the callsite (that is, see if it matches one the
         * VM already knows about). If it does, it will free the memory
         * associated and replace it with the interned one. Otherwise it
         * will store this one, provided it meets the interning rules. */
        MVM_callsite_try_intern(tc, &(callsites[i]));
    }

    /* Add one on to the maximum, to allow space for unshifting an extra
     * arg in the "supply invoked code object" case. */
    cu_body->max_callsite_size++;

    return callsites;
}

/* Creates code objects to go with each of the static frames. */
static void create_code_objects(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32  i;
    MVMObject *code_type;
    MVMCompUnitBody *cu_body = &cu->body;

    cu_body->coderefs = MVM_malloc(cu_body->num_frames * sizeof(MVMObject *));

    code_type = tc->instance->boot_types.BOOTCode;
    for (i = 0; i < cu_body->num_frames; i++) {
        MVMCode *coderef = (MVMCode *)REPR(code_type)->allocate(tc, STABLE(code_type));
        MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->coderefs[i], coderef);
        MVM_ASSIGN_REF(tc, &(coderef->common.header), coderef->body.sf, cu_body->frames[i]);
        MVM_ASSIGN_REF(tc, &(coderef->common.header), coderef->body.name, cu_body->frames[i]->body.name);
        MVM_ASSIGN_REF(tc, &(cu_body->frames[i]->common.header), cu_body->frames[i]->body.static_code, coderef);
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
    cu_body->orig_strings = rs->expected_strings;

    /* Load SC dependencies. */
    deserialize_sc_deps(tc, cu, rs);

    /* Load the extension op records. */
    cu_body->extops = deserialize_extop_records(tc, cu, rs);
    cu_body->num_extops = rs->expected_extops;

    /* Load the static frame info and give each one a code reference. */
    cu_body->frames = deserialize_frames(tc, cu, rs);
    cu_body->num_frames = rs->expected_frames;
    cu_body->orig_frames = rs->expected_frames;
    create_code_objects(tc, cu);

    /* Load callsites. */
    cu_body->max_callsite_size = MVM_MIN_CALLSITE_SIZE;
    cu_body->callsites = deserialize_callsites(tc, cu, rs);
    cu_body->num_callsites = rs->expected_callsites;
    cu_body->orig_callsites = rs->expected_callsites;

    /* Resolve HLL name. */
    MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->hll_name, cu_body->strings[rs->hll_str_idx]);

    /* Resolve special frames. */
    if (rs->main_frame)
        MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->main_frame, cu_body->frames[rs->main_frame - 1]);
    if (rs->load_frame)
        MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->load_frame, cu_body->frames[rs->load_frame - 1]);
    if (rs->deserialize_frame)
        MVM_ASSIGN_REF(tc, &(cu->common.header), cu_body->deserialize_frame, cu_body->frames[rs->deserialize_frame - 1]);

    /* Clean up reader state. */
    cleanup_all(tc, rs);

    /* Restore normal GC allocation. */
    MVM_gc_allocate_gen2_default_clear(tc);
}

/* returns the annotation for that bytecode offset */
MVMBytecodeAnnotation * MVM_bytecode_resolve_annotation(MVMThreadContext *tc, MVMStaticFrameBody *sfb, MVMuint32 offset) {
    MVMBytecodeAnnotation *ba = NULL;
    MVMuint32 i;

    if (sfb->num_annotations && offset < sfb->bytecode_size) {
        MVMuint8 *cur_anno = sfb->annotations_data;
        for (i = 0; i < sfb->num_annotations; i++) {
            MVMint32 ann_offset = read_int32(cur_anno, 0);
            if (ann_offset > offset)
                break;
            cur_anno += 12;
        }
        if (i)
            cur_anno -= 12;
        ba = MVM_malloc(sizeof(MVMBytecodeAnnotation));
        ba->bytecode_offset = read_int32(cur_anno, 0);
        ba->filename_string_heap_index = read_int32(cur_anno, 4);
        ba->line_number = read_int32(cur_anno, 8);
    }

    return ba;
}
