#include "moar.h"

#define line_length 1024
MVM_FORMAT(printf, 4, 5)
static void append_string(char **out, MVMuint32 *size,
        MVMuint32 *length, char *str, ...) {
    char string[line_length];
    MVMuint32 len;
    va_list args;
    va_start(args, str);

    vsnprintf(string, line_length, str, args);
    va_end(args);

    len = strlen(string);
    if (*length + len > *size) {
        while (*length + len > *size)
            *size = *size * 2;
        *out = MVM_realloc(*out, *size);
    }

    memcpy(*out + *length, string, len);
    *length = *length + len;
}

static const char * get_typename(MVMuint16 type) {
    switch(type) {
        case MVM_reg_int8 : return "int8";
        case MVM_reg_int16: return "int16";
        case MVM_reg_int32: return "int32";
        case MVM_reg_int64: return "int";
        case MVM_reg_num32: return "num32";
        case MVM_reg_num64: return "num";
        case MVM_reg_str  : return "str";
        case MVM_reg_obj  : return "obj";
        case MVM_reg_uint8 : return "uint8";
        case MVM_reg_uint16: return "uint16";
        case MVM_reg_uint32: return "uint32";
        case MVM_reg_uint64: return "uint";
        default           : fprintf(stderr, "unknown type %d\n", type);
                             return "UNKNOWN";
    }
}

#define a(...) append_string(&o,&s,&l, __VA_ARGS__)
/* Macros for getting things from the bytecode stream. */
/* GET_REG is defined differently here from interp.c */
#define GET_I8(pc, idx)     *((MVMint8 *)((pc) + (idx)))
#define GET_REG(pc, idx)    *((MVMuint16 *)((pc) + (idx)))
#define GET_I16(pc, idx)    *((MVMint16 *)((pc) + (idx)))
#define GET_UI16(pc, idx)   *((MVMuint16 *)((pc) + (idx)))
#define GET_I32(pc, idx)    *((MVMint32 *)((pc) + (idx)))
#define GET_UI32(pc, idx)   *((MVMuint32 *)((pc) + (idx)))
#define GET_N32(pc, idx)    *((MVMnum32 *)((pc) + (idx)))

enum {
    MVM_val_branch_target = 1,
    MVM_val_op_boundary   = 2
};

static MVMStaticFrame * get_frame(MVMThreadContext *tc, MVMCompUnit *cu, int idx) {
    return ((MVMCode *)cu->body.coderefs[idx])->body.sf;
}

static void bytecode_dump_frame_internal(MVMThreadContext *tc, MVMStaticFrame *frame, MVMSpeshCandidate *maybe_candidate, MVMuint8 *frame_cur_op, char ***frame_lexicals, char **oo, MVMuint32 *os, MVMuint32 *ol) {
    /* since "references" are not a thing in C, keep a local copy of these
     * and update the passed-in pointers at the end of the function */
    char *o = *oo;
    MVMuint32 s = *os;
    MVMuint32 l = *ol;

    MVMuint32 i, j;

    /* mostly stolen from validation.c */
    MVMStaticFrame *static_frame = frame;
    MVMuint32 bytecode_size = maybe_candidate ? maybe_candidate->body.bytecode_size : static_frame->body.bytecode_size;
    MVMuint8 *bytecode_start = maybe_candidate ? maybe_candidate->body.bytecode : static_frame->body.bytecode;
    MVMuint8 *bytecode_end = bytecode_start + bytecode_size;
    /* current position in the bytestream */
    MVMuint8 *cur_op = bytecode_start;
    /* positions in the bytestream that are starts of ops and goto targets */
    MVMuint8 *labels = MVM_calloc(1, bytecode_size);
    MVMuint32 *jumps = MVM_calloc(1, sizeof(MVMuint32) * bytecode_size);
    char **lines = MVM_malloc(sizeof(char *) * bytecode_size);
    MVMuint32 *linelocs = MVM_malloc(sizeof(MVMuint32) * bytecode_size);
    MVMuint32 lineno = 0;
    MVMuint32 lineloc;
    MVMuint16 op_num;
    const MVMOpInfo *op_info;
    MVMuint32 operand_size = 0;
    unsigned char op_rw;
    unsigned char op_type;
    unsigned char op_flags;
    MVMOpInfo tmp_extop_info;
    /* stash the outer output buffer */
    MVMuint32 sP = s;
    MVMuint32 lP = l;
    char *oP = o;
    char *tmpstr;
    char mark_this_line = 0;
    MVMCompUnit *cu = static_frame->body.cu;

    /* For handling var-arg ops like the dispatch ops */
    MVMOpInfo temporary_op_info;

    memset(&temporary_op_info, 0, sizeof(MVMOpInfo));

    while (cur_op < bytecode_end - 1) {

        /* allocate a line buffer */
        s = 200;
        l = 0;
        o = MVM_calloc(s, sizeof(char));

        lineloc = cur_op - bytecode_start;
        /* mark that this line starts at this point in the bytestream */
        linelocs[lineno] = lineloc;
        /* mark that this point in the bytestream is an op boundary */
        labels[lineloc] |= MVM_val_op_boundary;


        mark_this_line = 0;
        if (frame_cur_op) {
            if (frame_cur_op == cur_op || frame_cur_op == cur_op + 2) {
                mark_this_line = 1;
            }
        }

        if (mark_this_line) {
            a("-> ");
        } else {
            a("   ");
        }

        op_num = *((MVMint16 *)cur_op);
        cur_op += 2;
        if (op_num < MVM_OP_EXT_BASE) {
            op_info = MVM_op_get_op(op_num);
            if (op_info)
                a("%-18s ", op_info->name);
            else
                a("invalid OP        ");
        }
        else {
            MVMint16 ext_op_num = op_num - MVM_OP_EXT_BASE;
            if (0 <= ext_op_num && ext_op_num < cu->body.num_extops) {
                MVMExtOpRecord r = cu->body.extops[ext_op_num];
                MVMuint8 j;
                memset(&tmp_extop_info, 0, sizeof(MVMOpInfo));
                tmp_extop_info.name = MVM_string_utf8_encode_C_string(tc, r.name);
                memcpy(tmp_extop_info.operands, r.operand_descriptor, 8);
                for (j = 0; j < 8; j++)
                    if (tmp_extop_info.operands[j])
                        tmp_extop_info.num_operands++;
                    else
                        break;
                op_info = &tmp_extop_info;
                a("%-12s ", tmp_extop_info.name);
                MVM_free((void *)tmp_extop_info.name);
                tmp_extop_info.name = NULL;
            }
            else {
                a("Extension op %d out of range", (int)op_num);
                op_info = NULL;
            }
        }
        if (MVM_op_get_mark(op_num)[1] == 'd') {
            /* These are var-arg ops; grab callsite and synthesize op info */
            /* TODO this wants factoring out into a common function */
            MVMCallsite *callsite = cu->body.callsites[GET_UI16(cur_op, 4 + (op_num == MVM_OP_dispatch_v ? 0 : 2))];
            MVMuint16 operand_index;
            MVMuint16 flag_index;

            fprintf(stderr, "this callsite (%d) has %d args\n", GET_UI16(cur_op, 4 + (op_num == MVM_OP_dispatch_v ? 0 : 2)), callsite->flag_count);
            fprintf(stderr, "this op has %d args\n", op_info->num_operands);

            operand_index = op_num == MVM_OP_dispatch_v ? 2 : 3;

            memcpy(&temporary_op_info, op_info, sizeof(MVMOpInfo));
            temporary_op_info.num_operands += callsite->flag_count;

            for (flag_index = 0; operand_index < callsite->flag_count + op_info->num_operands; operand_index++, flag_index++) {
                MVMCallsiteFlags flag = callsite->arg_flags[flag_index];
                if (flag & MVM_CALLSITE_ARG_OBJ) {
                    temporary_op_info.operands[operand_index] = MVM_operand_obj;
                }
                else if (flag & MVM_CALLSITE_ARG_INT) {
                    temporary_op_info.operands[operand_index] = MVM_operand_int64;
                }
                else if (flag & MVM_CALLSITE_ARG_NUM) {
                    temporary_op_info.operands[operand_index] = MVM_operand_num64;
                }
                else if (flag & MVM_CALLSITE_ARG_STR) {
                    temporary_op_info.operands[operand_index] = MVM_operand_str;
                }
                temporary_op_info.operands[operand_index] |= MVM_operand_read_reg;
                fprintf(stderr, "(%d) %2x -> %4x ", operand_index, flag, temporary_op_info.operands[operand_index]);
            }
            fprintf(stderr, "\n");

            for (operand_index = 0; operand_index < temporary_op_info.num_operands; operand_index++) {
                fprintf(stderr, "%4x ", temporary_op_info.operands[operand_index]);
            }
            fprintf(stderr, "\n");

            op_info = &temporary_op_info;
        }

        if (!op_info)
            continue;

        for (i = 0; i < op_info->num_operands; i++) {
            if (i) a(", ");
            op_flags = op_info->operands[i];
            op_rw   = op_flags & MVM_operand_rw_mask;
            op_type = op_flags & MVM_operand_type_mask;

            if (op_rw == MVM_operand_literal) {
                switch (op_type) {
                    case MVM_operand_int8:
                        operand_size = 1;
                        a("%"PRId8, GET_I8(cur_op, 0));
                        break;
                    case MVM_operand_int16:
                        operand_size = 2;
                        a("%"PRId16, GET_I16(cur_op, 0));
                        break;
                    case MVM_operand_int32:
                        operand_size = 4;
                        a("%"PRId32, GET_I32(cur_op, 0));
                        break;
                    case MVM_operand_int64:
                        operand_size = 8;
                        a("%"PRId64, MVM_BC_get_I64(cur_op, 0));
                        break;
                    case MVM_operand_uint8:
                        operand_size = 1;
                        a("%"PRIu8, GET_I8(cur_op, 0));
                        break;
                    case MVM_operand_uint16:
                        operand_size = 2;
                        a("%"PRIu16, GET_I16(cur_op, 0));
                        break;
                    case MVM_operand_uint32:
                        operand_size = 4;
                        a("%"PRIu32, GET_I32(cur_op, 0));
                        break;
                    case MVM_operand_uint64:
                        operand_size = 8;
                        a("%"PRIu64, MVM_BC_get_I64(cur_op, 0));
                        break;
                    case MVM_operand_num32:
                        operand_size = 4;
                        a("%f", GET_N32(cur_op, 0));
                        break;
                    case MVM_operand_num64:
                        operand_size = 8;
                        a("%f", MVM_BC_get_N64(cur_op, 0));
                        break;
                    case MVM_operand_callsite:
                        operand_size = 2;
                        a("Callsite_%"PRIu16, GET_UI16(cur_op, 0));
                        break;
                    case MVM_operand_coderef:
                        operand_size = 2;
                        a("Frame_%"PRIu16, GET_UI16(cur_op, 0));
                        break;
                    case MVM_operand_str:
                        operand_size = 4;
                        if (GET_UI32(cur_op, 0) < cu->body.num_strings) {
                            tmpstr = MVM_string_utf8_encode_C_string(
                                    tc, MVM_cu_string(tc, cu, GET_UI32(cur_op, 0)));
                            /* XXX C-string-literal escape the \ and '
                                and line breaks and non-ascii someday */
                            a("'%s'", tmpstr);
                            MVM_free(tmpstr);
                        }
                        else
                            a("invalid string index: %d", GET_UI32(cur_op, 0));
                        break;
                    case MVM_operand_ins:
                        operand_size = 4;
                        /* luckily all the ins operands are at the end
                        of op operands, so I can wait to resolve the label
                        to the end. */
                        if (GET_UI32(cur_op, 0) < bytecode_size) {
                            labels[GET_UI32(cur_op, 0)] |= MVM_val_branch_target;
                            jumps[lineno] = GET_UI32(cur_op, 0);
                        }
                        break;
                    case MVM_operand_obj:
                        /* not sure what a literal object is */
                        operand_size = 4;
                        break;
                    case MVM_operand_spesh_slot:
                        operand_size = 2;
                        a("sslot(%d)", GET_UI16(cur_op, 0));
                        break;
                    default:
                        fprintf(stderr, "what is an operand of type %d??\n", op_type);
                        abort(); /* never reached, silence compiler warnings */
                }
            }
            else if (op_rw == MVM_operand_read_reg || op_rw == MVM_operand_write_reg) {
                /* register operand */
                MVMuint8 frame_has_inlines = maybe_candidate && maybe_candidate->body.num_inlines ? 1 : 0;
                MVMuint16 *local_types = frame_has_inlines ? maybe_candidate->body.local_types : frame->body.local_types;
                operand_size = 2;
                a("loc_%u_%s", GET_REG(cur_op, 0),
                    local_types ? get_typename(local_types[GET_REG(cur_op, 0)]) : "unknown");
            }
            else if (op_rw == MVM_operand_read_lex || op_rw == MVM_operand_write_lex) {
                /* lexical operand */
                MVMuint16 idx, frames, m;
                MVMStaticFrame *applicable_frame = static_frame;

                operand_size = 4;
                idx = GET_UI16(cur_op, 0);
                frames = GET_UI16(cur_op, 2);

                m = frames;
                while (m > 0) {
                    applicable_frame = applicable_frame->body.outer;
                    m--;
                }
                /* inefficient, I know. should use a hash. */
                for (m = 0; m < cu->body.num_frames; m++) {
                    if (get_frame(tc, cu, m) == applicable_frame) {
                        if (frame_lexicals) {
                            char *lexname = frame_lexicals[m][idx];
                            a("lex_Frame_%u_%s_%s", m, lexname,
                                get_typename(applicable_frame->body.lexical_types[idx]));
                        }
                        else {
                            a("lex_Frame_%u_lex%d_%s", m, idx,
                                get_typename(applicable_frame->body.lexical_types[idx]));
                        }
                    }
                }
            }
            cur_op += operand_size;
        }

        lines[lineno++] = o;
    }
    {
        MVMuint32 *linelabels = MVM_calloc(lineno, sizeof(MVMuint32));
        MVMuint32 byte_offset = 0;
        MVMuint32 line_number = 0;
        MVMuint32 label_number = 1;
        MVMuint32 *annotations = MVM_calloc(lineno, sizeof(MVMuint32));

        for (; byte_offset < bytecode_size; byte_offset++) {
            if (labels[byte_offset] & MVM_val_branch_target) {
                /* found a byte_offset where a label should be.
                 now crawl up through the lines to find which line starts there */
                while (line_number < lineno && linelocs[line_number] != byte_offset) line_number++;
                if (line_number < lineno)
                    linelabels[line_number] = label_number++;
            }
        }
        o = oP;
        l = lP;
        s = sP;

        i = 0;
        /* resolve annotation line numbers */
        for (j = 0; j < frame->body.num_annotations; j++) {
            MVMuint32 ann_offset = GET_UI32(frame->body.annotations_data, j*12);
            for (; i < lineno; i++) {
                if (linelocs[i] == ann_offset) {
                    annotations[i] = j + 1;
                    break;
                }
            }
        }

        for (j = 0; j < lineno; j++) {
            if (annotations[j]) {
                MVMuint16 shi = GET_UI16(frame->body.annotations_data + 4, (annotations[j] - 1)*12);
                tmpstr = MVM_string_utf8_encode_C_string(
                    tc, MVM_cu_string(tc, cu, shi < cu->body.num_strings ? shi : 0));
                a("     annotation: %s:%u\n", tmpstr, GET_UI32(frame->body.annotations_data, (annotations[j] - 1)*12 + 8));
                MVM_free(tmpstr);
            }
            if (linelabels[j])
                a("     label_%u:\n", linelabels[j]);
            a("%05d   %s", j, lines[j]);
            MVM_free(lines[j]);
            if (jumps[j]) {
                /* horribly inefficient for large frames.  again, should use a hash */
                line_number = 0;
                while (line_number < lineno && linelocs[line_number] != jumps[j]) line_number++;
                if (line_number < lineno)
                    a("label_%u(%05u)", linelabels[line_number], line_number);
                else
                    a("label (invalid: %05u)", jumps[j]);
            }
            a("\n");
        }
        MVM_free(lines);
        MVM_free(jumps);
        MVM_free(linelocs);
        MVM_free(linelabels);
        MVM_free(labels);
        MVM_free(annotations);
    }

    *oo = o;
    *os = s;
    *ol = l;
}


char * MVM_bytecode_dump(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32 s = 1024;
    MVMuint32 l = 0;
    MVMuint32 i, j, k;
    char *o = MVM_calloc(s, sizeof(char));
    char ***frame_lexicals = MVM_malloc(sizeof(char **) * cu->body.num_frames);

    a("\nMoarVM dump of binary compilation unit:\n\n");

    for (k = 0; k < cu->body.num_scs; k++) {
        char *tmpstr = MVM_string_utf8_encode_C_string(
            tc, MVM_cu_string(tc, cu, cu->body.sc_handle_idxs[k]));
        a("  SC_%u : %s\n", k, tmpstr);
        MVM_free(tmpstr);
    }

    for (k = 0; k < cu->body.num_callsites; k++) {
        MVMCallsite *callsite  = cu->body.callsites[k];
        MVMuint16 arg_count    = callsite->arg_count;
        MVMuint16 nameds_count = 0;

        a("  Callsite_%u :\n", k);
        a("    num_pos: %d\n", callsite->num_pos);
        a("    arg_count: %u\n", arg_count);
        for (j = 0, i = 0; j < arg_count; j++) {
            MVMCallsiteEntry csitee = callsite->arg_flags[i++];
            a("    Arg %u :", i);
            if (csitee & MVM_CALLSITE_ARG_NAMED) {
                if (callsite->arg_names) {
                    char *arg_name = MVM_string_utf8_encode_C_string(tc,
                        callsite->arg_names[nameds_count++]);
                    a(" named(%s)", arg_name);
                    MVM_free(arg_name);
                }
                else {
                    a(" named");
                }
                j++;
            }
            else if (csitee & MVM_CALLSITE_ARG_FLAT_NAMED) {
                a(" flatnamed");
            }
            else if (csitee & MVM_CALLSITE_ARG_FLAT) {
                a(" flat");
            }
            else a(" positional");
            if (csitee & MVM_CALLSITE_ARG_OBJ) a(" obj");
            else if (csitee & MVM_CALLSITE_ARG_INT) a(" int");
            else if (csitee & MVM_CALLSITE_ARG_NUM) a(" num");
            else if (csitee & MVM_CALLSITE_ARG_STR) a(" str");
            if (csitee & MVM_CALLSITE_ARG_FLAT) a(" flat");
            a("\n");
        }
    }
    for (k = 0; k < cu->body.num_frames; k++)
        MVM_bytecode_finish_frame(tc, cu, get_frame(tc, cu, k), 1);

    for (k = 0; k < cu->body.num_frames; k++) {
        MVMStaticFrame *frame = get_frame(tc, cu, k);

        if (!frame->body.fully_deserialized) {
            MVM_bytecode_finish_frame(tc, cu, frame, 1);
        }

        MVMuint32 num_lexicals = frame->body.num_lexicals;
        if (num_lexicals) {
            MVMString **lexical_names_list = frame->body.lexical_names_list;

            char **lexicals = (char **)MVM_malloc(sizeof(char *) * num_lexicals);
            for (j = 0; j < num_lexicals; j++) {
                lexicals[j]   = MVM_string_utf8_encode_C_string(tc, lexical_names_list[j]);
            }
            frame_lexicals[k] = lexicals;
        }
        else {
            frame_lexicals[k] = NULL;
        }
    }
    for (k = 0; k < cu->body.num_frames; k++) {
        MVMStaticFrame *frame = get_frame(tc, cu, k);
        char *cuuid;
        char *fname;
        cuuid = MVM_string_utf8_encode_C_string(tc, frame->body.cuuid);
        fname = MVM_string_utf8_encode_C_string(tc, frame->body.name);
        a("  Frame_%u :\n", k);
        a("    cuuid : %s\n", cuuid);
        MVM_free(cuuid);
        a("    name : %s\n", fname);
        MVM_free(fname);
        for (j = 0; j < cu->body.num_frames; j++) {
            if (get_frame(tc, cu, j) == frame->body.outer)
                a("    outer : Frame_%u\n", j);
        }

        for (j = 0; j < frame->body.num_locals; j++) {
            if (!j)
                a("    Locals :\n");
            a("  %6u: loc_%u_%s\n", j, j, get_typename(frame->body.local_types[j]));
        }

        for (j = 0; j < frame->body.num_lexicals; j++) {
            if (!j)
                a("    Lexicals :\n");
            a("  %6u: lex_Frame_%u_%s_%s\n", j, k, frame_lexicals[k][j], get_typename(frame->body.lexical_types[j]));
        }
        a("    Instructions :\n");
        {
            bytecode_dump_frame_internal(tc, frame, NULL, NULL, frame_lexicals, &o, &s, &l);
        }
    }

    o[l] = '\0';

    for (k = 0; k < cu->body.num_frames; k++) {
        for (j = 0; j < get_frame(tc, cu, k)->body.num_lexicals; j++) {
            MVM_free(frame_lexicals[k][j]);
        }
        MVM_free(frame_lexicals[k]);
    }
    MVM_free(frame_lexicals);
    return o;
}

#ifdef DEBUG_HELPERS
void MVM_dump_bytecode_of(MVMThreadContext *tc, MVMFrame *frame, MVMSpeshCandidate *maybe_candidate) {
    MVMuint32 s = 1024;
    MVMuint32 l = 0;
    char *o = MVM_calloc(s, sizeof(char));
    MVMuint8 *addr;

    if (!frame) {
        frame = tc->cur_frame;
        addr = *tc->interp_cur_op;
    } else {
        addr = frame->return_address;
        if (!addr) {
            addr = *tc->interp_cur_op;
        }
    }

    bytecode_dump_frame_internal(tc, frame->static_info, maybe_candidate, addr, NULL, &o, &s, &l);

    o[l] = 0;

    fprintf(stderr, "%s", o);
}

void MVM_dump_bytecode_staticframe(MVMThreadContext *tc, MVMStaticFrame *frame) {
    MVMuint32 s = 1024;
    MVMuint32 l = 0;
    char *o = MVM_calloc(s, sizeof(char));

    bytecode_dump_frame_internal(tc, frame, NULL, NULL, NULL, &o, &s, &l);

    o[l] = 0;

    fprintf(stderr, "%s", o);
}

void MVM_dump_bytecode(MVMThreadContext *tc) {
    MVMStaticFrame *sf = tc->cur_frame->static_info;
    MVMuint8 *effective_bytecode = MVM_frame_effective_bytecode(tc->cur_frame);
    if (effective_bytecode == sf->body.bytecode) {
        MVM_dump_bytecode_of(tc, tc->cur_frame, NULL);
    } else {
        MVM_dump_bytecode_of(tc, tc->cur_frame, tc->cur_frame->spesh_cand);
        /*MVMint32 spesh_cand_idx;*/
        /*MVMuint8 found = 0;*/
        /*for (spesh_cand_idx = 0; spesh_cand_idx < sf->body.num_spesh_candidates; spesh_cand_idx++) {*/
            /*MVMSpeshCandidate *cand = sf->body.spesh_candidates[spesh_cand_idx];*/
            /*if (cand->body.bytecode == effective_bytecode) {*/
                /*MVM_dump_bytecode_of(tc, tc->cur_frame, cand);*/
                /*found = 1;*/
            /*}*/
        /*}*/
        /*if (!found) {*/
            /* It's likely the MAGIC_BYTECODE from the jit?
             * in that case we just grab tc->cur_frame->spesh_cand apparently */
        /*}*/
    }
}

void MVM_dump_bytecode_stackframe(MVMThreadContext *tc, MVMint32 depth) {
    MVMStaticFrame *sf;
    MVMuint8 *effective_bytecode;
    MVMFrame *frame = tc->cur_frame;
    for (;depth > 0; depth--) {
        frame = frame->caller;
    }
    sf = frame->static_info;
    effective_bytecode = MVM_frame_effective_bytecode(frame);
    if (effective_bytecode == sf->body.bytecode) {
        MVM_dump_bytecode_of(tc, frame, NULL);
    } else {
        MVMuint32 spesh_cand_idx;
        MVMStaticFrameSpesh *spesh = sf->body.spesh;
        for (spesh_cand_idx = 0; spesh_cand_idx < spesh->body.num_spesh_candidates; spesh_cand_idx++) {
            MVMSpeshCandidate *cand = spesh->body.spesh_candidates[spesh_cand_idx];
            if (cand->body.bytecode == effective_bytecode) {
                MVM_dump_bytecode_of(tc, frame, cand);
            }
        }
    }
}
#endif
