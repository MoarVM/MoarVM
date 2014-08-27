#include "moar.h"

/* Auto-growing buffer. */
typedef struct {
    char   *buffer;
    size_t  alloc;
    size_t  pos;
} DumpStr;
static void append(DumpStr *ds, char *to_add) {
    size_t len = strlen(to_add);
    if (ds->pos + len >= ds->alloc) {
        ds->alloc *= 4;
        if (ds->pos + len >= ds->alloc)
            ds->alloc += len;
        ds->buffer = MVM_realloc(ds->buffer, ds->alloc);
    }
    memcpy(ds->buffer + ds->pos, to_add, len);
    ds->pos += len;
}

/* Formats a string and then appends it. */
static void appendf(DumpStr *ds, const char *fmt, ...) {
    char *c_message = MVM_malloc(1024);
    va_list args;
    va_start(args, fmt);
    c_message[vsnprintf(c_message, 1023, fmt, args)] = 0;
    append(ds, c_message);
    MVM_free(c_message);
    va_end(args);
}

/* Turns a MoarVM string into a C string and appends it. */
static void append_str(MVMThreadContext *tc, DumpStr *ds, MVMString *s) {
    MVMuint8 *cs = MVM_string_utf8_encode_C_string(tc, s);
    append(ds, cs);
    MVM_free(cs);
}

/* Appends a null at the end. */
static void append_null(DumpStr *ds) {
    append(ds, " "); /* Delegate realloc if we're really unlucky. */
    ds->buffer[ds->pos - 1] = '\0';
}

/* Dumps a basic block. */
static void dump_bb(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMSpeshIns *cur_ins;
    MVMint64     i;

    /* Heading. */
    appendf(ds, "  BB %d:\n", bb->idx);

    /* Instructions. */
    append(ds, "    Instructions:\n");
    cur_ins = bb->first_ins;
    while (cur_ins) {
        MVMSpeshAnn *ann = cur_ins->annotations;
        while (ann) {
            switch (ann->type) {
                case MVM_SPESH_ANN_FH_START:
                    appendf(ds, "      [Annotation: FH Start (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_END:
                    appendf(ds, "      [Annotation: FH End (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_GOTO:
                    appendf(ds, "      [Annotation: FH Goto (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                    appendf(ds, "      [Annotation: INS Deopt One (idx %d -> pc %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx]);
                    break;
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    appendf(ds, "      [Annotation: INS Deopt All (idx %d -> pc %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx]);
                    break;
                case MVM_SPESH_ANN_INLINE_START:
                    appendf(ds, "      [Annotation: Inline Start (%d)]\n",
                        ann->data.inline_idx);
                    break;
                case MVM_SPESH_ANN_INLINE_END:
                    appendf(ds, "      [Annotation: Inline End (%d)]\n",
                        ann->data.inline_idx);
                    break;
                case MVM_SPESH_ANN_DEOPT_INLINE:
                    appendf(ds, "      [Annotation: INS Deopt Inline (idx %d -> pc %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx]);
                    break;
                case MVM_SPESH_ANN_DEOPT_OSR:
                    appendf(ds, "      [Annotation: INS Deopt OSR (idx %d -> pc %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx]);
                    break;
                default:
                    appendf(ds, "      [Annotation: %d (unknown)]\n", ann->type);
            }
            ann = ann->next;
        }

        appendf(ds, "      %s ", cur_ins->info->name);
        if (cur_ins->info->opcode == MVM_SSA_PHI) {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    append(ds, ", ");
                appendf(ds, "r%d(%d)", cur_ins->operands[i].reg.orig,
                    cur_ins->operands[i].reg.i);
            }
        }
        else {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    append(ds, ", ");
                switch (cur_ins->info->operands[i] & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg:
                        appendf(ds, "r%d(%d)", cur_ins->operands[i].reg.orig,
                            cur_ins->operands[i].reg.i);
                        break;
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex:
                        appendf(ds, "lex(idx=%d,outers=%d)", cur_ins->operands[i].lex.idx,
                            cur_ins->operands[i].lex.outers);
                        break;
                    case MVM_operand_literal: {
                        MVMuint32 type = cur_ins->info->operands[i] & MVM_operand_type_mask;
                        switch (type) {
                        case MVM_operand_ins:
                            appendf(ds, "BB(%d)", cur_ins->operands[i].ins_bb->idx);
                            break;
                        case MVM_operand_int8:
                            appendf(ds, "liti8(%d)", cur_ins->operands[i].lit_i8);
                            break;
                        case MVM_operand_int16:
                            appendf(ds, "liti16(%d)", cur_ins->operands[i].lit_i16);
                            break;
                        case MVM_operand_int32:
                            appendf(ds, "liti32(%d)", cur_ins->operands[i].lit_i32);
                            break;
                        case MVM_operand_int64:
                            appendf(ds, "liti64(%d)", cur_ins->operands[i].lit_i64);
                            break;
                        case MVM_operand_num32:
                            appendf(ds, "litn32(%f)", cur_ins->operands[i].lit_n32);
                            break;
                        case MVM_operand_num64:
                            appendf(ds, "litn64(%g)", cur_ins->operands[i].lit_n64);
                            break;
                        case MVM_operand_str: {
                            char *cstr = MVM_string_utf8_encode_C_string(tc, g->sf->body.cu->body.strings[cur_ins->operands[i].lit_str_idx]);
                            appendf(ds, "lits(%s)", cstr);
                            MVM_free(cstr);
                            break;
                        }
                        case MVM_operand_spesh_slot:
                            appendf(ds, "sslot(%d)", cur_ins->operands[i].lit_i16);
                            break;
                        default:
                            append(ds, "<nyi(lit)>");
                        }
                        break;
                    }
                    default:
                        append(ds, "<nyi>");
                }
            }
        }
        append(ds, "\n");
        cur_ins = cur_ins->next;
    }

    /* Predecessors and successors. */
    append(ds, "    Successors: ");
    for (i = 0; i < bb->num_succ; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->succ[i]->idx);
    append(ds, "\n    Predeccessors: ");
    for (i = 0; i < bb->num_pred; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->pred[i]->idx);
    append(ds, "\n    Dominance children: ");
    for (i = 0; i < bb->num_children; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->children[i]->idx);
    append(ds, "\n\n");
}

/* Dumps the facts table. */
static void dump_facts(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i, j, num_locals, num_facts;
    num_locals = g->num_locals;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 usages = g->facts[i][j].usages;
            MVMint32 flags  = g->facts[i][j].flags;
            appendf(ds, "    r%d(%d): usages=%d, flags=%d\n", i, j, usages, flags);
        }
    }
}

static void dump_fileinfo(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &g->sf->body, 0);
    MVMCompUnit            *cu = g->sf->body.cu;
    MVMint32           str_idx = ann ? ann->filename_string_heap_index : 0;
    MVMint32           line_nr = ann ? ann->line_number : 1;
    MVMString        *filename = cu->body.filename;
    MVMuint8    *filename_utf8 = "<unknown>";
    if (ann && str_idx < cu->body.num_strings) {
        filename = cu->body.strings[str_idx];
    }
    if (filename)
        filename_utf8 = MVM_string_utf8_encode(tc, filename, NULL);
    appendf(ds, "%s:%d", filename_utf8, line_nr);
    if (filename)
        MVM_free(filename_utf8);
}

/* Dump a spesh graph into string form, for debugging purposes. */
char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb;

    /* Allocate buffer. */
    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    append(&ds, "Spesh of '");
    append_str(tc, &ds, g->sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, g->sf->body.cuuid);
    append(&ds, ", file: ");
    dump_fileinfo(tc, &ds, g);
    append(&ds, ")\n\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, &ds, g, cur_bb);
        cur_bb = cur_bb->linear_next;
    }

    /* Dump facts. */
    append(&ds, "\nFacts:\n");
    dump_facts(tc, &ds, g);

    append(&ds, "\n");
    append_null(&ds);
    return ds.buffer;
}
