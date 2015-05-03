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
        default           : return "UNKNOWN";
    }
}

#define a(...) append_string(&o,&s,&l, __VA_ARGS__)
/* Macros for getting things from the bytecode stream. */
/* GET_REG is defined differently here from interp.c */
#define GET_I8(pc, idx)     *((MVMint8 *)(pc + idx))
#define GET_REG(pc, idx)    *((MVMuint16 *)(pc + idx))
#define GET_I16(pc, idx)    *((MVMint16 *)(pc + idx))
#define GET_UI16(pc, idx)   *((MVMuint16 *)(pc + idx))
#define GET_I32(pc, idx)    *((MVMint32 *)(pc + idx))
#define GET_UI32(pc, idx)   *((MVMuint32 *)(pc + idx))
#define GET_N32(pc, idx)    *((MVMnum32 *)(pc + idx))

enum {
    MVM_val_branch_target = 1,
    MVM_val_op_boundary   = 2
};

char * MVM_bytecode_dump(MVMThreadContext *tc, MVMCompUnit *cu) {
    MVMuint32 s = 1024;
    MVMuint32 l = 0;
    MVMuint32 i, j, k;
    char *o = MVM_calloc(sizeof(char) * s, 1);
    char ***frame_lexicals = MVM_malloc(sizeof(char **) * cu->body.num_frames);
    MVMString *name = MVM_string_utf8_decode(tc, tc->instance->VMString, "", 0);

    a("\nMoarVM dump of binary compilation unit:\n\n");

    for (k = 0; k < cu->body.num_scs; k++) {
        char *tmpstr = MVM_string_utf8_encode_C_string(
            tc, cu->body.strings[cu->body.sc_handle_idxs[k]]);
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
        MVM_bytecode_finish_frame(tc, cu, cu->body.frames[k], 1);

    for (k = 0; k < cu->body.num_frames; k++) {
        MVMStaticFrame *frame = cu->body.frames[k];
        MVMLexicalRegistry *current, *tmp;
        unsigned bucket_tmp;
        char **lexicals;

        if (!frame->body.fully_deserialized) {
            MVM_bytecode_finish_frame(tc, cu, frame, 1);
        }

        lexicals = (char **)MVM_malloc(sizeof(char *) * frame->body.num_lexicals);
        frame_lexicals[k] = lexicals;

        HASH_ITER(hash_handle, frame->body.lexical_names, current, tmp, bucket_tmp) {
            name->body.storage.blob_32 = (MVMint32 *)current->hash_handle.key;
            name->body.num_graphs      = (MVMuint32)current->hash_handle.keylen / sizeof(MVMGrapheme32);
            lexicals[current->value]   = MVM_string_utf8_encode_C_string(tc, name);
        }
    }
    for (k = 0; k < cu->body.num_frames; k++) {
        MVMStaticFrame *frame = cu->body.frames[k];
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
            if (cu->body.frames[j] == frame->body.outer)
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

    /* mostly stolen from validation.c */
    MVMStaticFrame *static_frame = frame;
    MVMuint32 bytecode_size = static_frame->body.bytecode_size;
    MVMuint8 *bytecode_start = static_frame->body.bytecode;
    MVMuint8 *bytecode_end = bytecode_start + bytecode_size;
    /* current position in the bytestream */
    MVMuint8 *cur_op = bytecode_start;
    /* positions in the bytestream that are starts of ops and goto targets */
    MVMuint8 *labels = MVM_calloc(bytecode_size, 1);
    MVMuint32 *jumps = MVM_calloc(sizeof(MVMuint32) * bytecode_size, 1);
    char **lines = MVM_malloc(sizeof(char *) * bytecode_size);
    MVMuint32 *linelocs = MVM_malloc(bytecode_size);
    MVMuint32 lineno = 0;
    MVMuint32 lineloc;
    MVMuint16 op_num;
    const MVMOpInfo *op_info;
    MVMuint32 operand_size;
    unsigned char op_rw;
    unsigned char op_type;
    unsigned char op_flags;
    MVMOpInfo tmp_extop_info;
    /* stash the outer output buffer */
    MVMuint32 sP = s;
    MVMuint32 lP = l;
    char *oP = o;
    char *tmpstr;
    while (cur_op < bytecode_end - 1) {

        /* allocate a line buffer */
        s = 200;
        l = 0;
        o = MVM_calloc(sizeof(char) * s, 1);

        lineloc = cur_op - bytecode_start;
        /* mark that this line starts at this point in the bytestream */
        linelocs[lineno] = lineloc;
        /* mark that this point in the bytestream is an op boundary */
        labels[lineloc] |= MVM_val_op_boundary;

        op_num = *((MVMint16 *)cur_op);
        cur_op += 2;
        if (op_num < MVM_OP_EXT_BASE) {
            op_info = MVM_op_get_op(op_num);
            a("%-12s ", op_info->name);
        }
        else {
            MVMint16 ext_op_num = op_num - MVM_OP_EXT_BASE;
            if (ext_op_num < cu->body.num_extops) {
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
                MVM_exception_throw_adhoc(tc, "Extension op %d out of range", (int)op_num);
            }
        }

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
                        tmpstr = MVM_string_utf8_encode_C_string(
                            tc, cu->body.strings[GET_UI32(cur_op, 0)]);
                        /* XXX C-string-literal escape the \ and '
                            and line breaks and non-ascii someday */
                        a("'%s'", tmpstr);
                        MVM_free(tmpstr);
                        break;
                    case MVM_operand_ins:
                        operand_size = 4;
                        /* luckily all the ins operands are at the end
                        of op operands, so I can wait to resolve the label
                        to the end. */
                        labels[GET_UI32(cur_op, 0)] |= MVM_val_branch_target;
                        jumps[lineno] = GET_UI32(cur_op, 0);
                        break;
                    case MVM_operand_obj:
                        /* not sure what a literal object is */
                        operand_size = 4;
                        break;
                    default:
                        abort(); /* never reached, silence compiler warnings */
                }
            }
            else if (op_rw == MVM_operand_read_reg || op_rw == MVM_operand_write_reg) {
                /* register operand */
                operand_size = 2;
                a("loc_%u_%s", GET_REG(cur_op, 0),
                    get_typename(frame->body.local_types[GET_REG(cur_op, 0)]));
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
                    if (cu->body.frames[m] == applicable_frame) {
                        a("lex_Frame_%u_%s_%s", m, frame_lexicals[m][idx],
                            get_typename(applicable_frame->body.lexical_types[idx]));
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
                while (linelocs[line_number] != byte_offset) line_number++;
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
                    tc, cu->body.strings[
						shi < cu->body.num_strings ? shi : 0
					]);
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
                while (linelocs[line_number] != jumps[j]) line_number++;
                a("label_%u(%05u)", linelabels[line_number], line_number);
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

        }
    }
    for (k = 0; k < cu->body.num_frames; k++) {
        for (j = 0; j < cu->body.frames[k]->body.num_lexicals; j++) {
            MVM_free(frame_lexicals[k][j]);
        }
        MVM_free(frame_lexicals[k]);
    }
    MVM_free(frame_lexicals);
    return o;
}
