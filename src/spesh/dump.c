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

static size_t tell_ds(DumpStr *ds) {
    return ds->pos;
}

static void rewind_ds(DumpStr *ds, size_t target) {
    if (ds->pos > target) {
        ds->pos = target;
        ds->buffer[ds->pos + 1] = '\0';
    }
}

/* Formats a string and then appends it. */
MVM_FORMAT(printf, 2, 3)
static void appendf(DumpStr *ds, const char *fmt, ...) {
    char *c_message = MVM_malloc(1024);
    va_list args;
    va_start(args, fmt);
    vsnprintf(c_message, 1023, fmt, args);
    append(ds, c_message);
    MVM_free(c_message);
    va_end(args);
}

/* Turns a MoarVM string into a C string and appends it. */
static void append_str(MVMThreadContext *tc, DumpStr *ds, MVMString *s) {
    char *cs = MVM_string_utf8_encode_C_string(tc, s);
    append(ds, cs);
    MVM_free(cs);
}

/* Appends a null at the end. */
static void append_null(DumpStr *ds) {
    append(ds, " "); /* Delegate realloc if we're really unlucky. */
    ds->buffer[ds->pos - 1] = '\0';
}

typedef struct {
    MVMuint32 total_size;
    MVMuint32 inlined_size;
} SpeshGraphSizeStats;

typedef struct {
    MVMint32 cur_depth;
    MVMint32 inline_idx[64];
} InlineIndexStack;

static void push_inline(MVMThreadContext *tc, InlineIndexStack *stack, MVMint32 idx) {
    if (stack->cur_depth == 63)
        MVM_oops(tc, "Too many levels of inlining to dump");
    stack->cur_depth++;
    stack->inline_idx[stack->cur_depth] = idx;
}

static void pop_inline(MVMThreadContext *tc, InlineIndexStack *stack) {
    stack->cur_depth--;
    if (stack->cur_depth < -1)
        MVM_oops(tc, "Too many levels of inlining popped");
}

static MVMCompUnit * get_current_cu(MVMThreadContext *tc, MVMSpeshGraph *g, InlineIndexStack *stack) {
    if (stack->cur_depth < 0)
        return g->sf->body.cu;
    else
        return g->inlines[stack->inline_idx[stack->cur_depth]].sf->body.cu;
}

/* Dumps a basic block. */
static void dump_bb(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g, MVMSpeshBB *bb,
                    SpeshGraphSizeStats *stats, InlineIndexStack *inline_stack) {
    MVMSpeshIns *cur_ins;
    MVMint64     i;
    MVMint32     size = 0;

    /* Heading. */
    appendf(ds, "  BB %d (%p):\n", bb->idx, bb);

    if (bb->inlined) {
        append(ds, "    Inlined\n");
    }

    {
        /* Also, we have a line number */
        MVMBytecodeAnnotation *bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
        MVMuint32 line_number;
        if (bbba) {
            line_number = bbba->line_number;
            MVM_free(bbba);
        } else {
            line_number = -1;
        }
        appendf(ds, "    line: %d (pc %d)\n", line_number, bb->initial_pc);
    }

    /* Instructions. */
    append(ds, "    Instructions:\n");
    cur_ins = bb->first_ins;
    while (cur_ins) {
        MVMSpeshAnn *ann = cur_ins->annotations;
        MVMuint32 line_number = -1;
        MVMuint32 pop_inlines = 0;
        MVMuint32 num_comments = 0;

        while (ann) {
            /* These four annotations carry a deopt index that we can find a
             * corresponding line number for */
            if (ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS
                || ann->type == MVM_SPESH_ANN_DEOPT_ALL_INS
                || ann->type == MVM_SPESH_ANN_DEOPT_INLINE
                || ann->type == MVM_SPESH_ANN_DEOPT_OSR) {
                MVMBytecodeAnnotation *ba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, g->deopt_addrs[2 * ann->data.deopt_idx]);
                if (ba) {
                    line_number = ba->line_number;
                    MVM_free(ba);
                } else {
                    line_number = -1;
                }
            }
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
                    appendf(ds, "      [Annotation: INS Deopt One (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    appendf(ds, "      [Annotation: INS Deopt All (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_INLINE_START:
                    appendf(ds, "      [Annotation: Inline Start (%d)]\n",
                        ann->data.inline_idx);
                    push_inline(tc, inline_stack, ann->data.inline_idx);
                    break;
                case MVM_SPESH_ANN_INLINE_END:
                    appendf(ds, "      [Annotation: Inline End (%d)]\n",
                        ann->data.inline_idx);
                    pop_inlines++;
                    break;
                case MVM_SPESH_ANN_DEOPT_INLINE:
                    appendf(ds, "      [Annotation: INS Deopt Inline (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_DEOPT_OSR:
                    appendf(ds, "      [Annotation: INS Deopt OSR (idx %d -> pc %d); line %d]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_LINENO: {
                    char *cstr;
                    MVMCompUnit *cu = get_current_cu(tc, g, inline_stack);
                    if (cu->body.num_strings < ann->data.lineno.filename_string_index) {
                        appendf(ds, "      [Annotation: Line Number: <out of bounds>:%d]\n",
                        ann->data.lineno.line_number);
                    }
                    else {
                        cstr = MVM_string_utf8_encode_C_string(tc,
                            MVM_cu_string(tc, get_current_cu(tc, g, inline_stack),
                            ann->data.lineno.filename_string_index));
                        appendf(ds, "      [Annotation: Line Number: %s:%d]\n",
                            cstr, ann->data.lineno.line_number);
                        MVM_free(cstr);
                    }
                    break;
                }
                case MVM_SPESH_ANN_LOGGED:
                    appendf(ds, "      [Annotation: Logged (bytecode offset %d)]\n",
                        ann->data.bytecode_offset);
                    break;
                case MVM_SPESH_ANN_DEOPT_SYNTH:
                    appendf(ds, "      [Annotation: INS Deopt Synth (idx %d)]\n",
                        ann->data.deopt_idx);
                    break;
                case MVM_SPESH_ANN_COMMENT:
                    num_comments++;
                    break;
                default:
                    appendf(ds, "      [Annotation: %d (unknown)]\n", ann->type);
            }
            ann = ann->next;
        }
        while (pop_inlines--)
            pop_inline(tc, inline_stack);

        if (num_comments > 1) {
            ann = cur_ins->annotations;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_COMMENT) {
                    appendf(ds, "      # [%03d] %s\n", ann->order, ann->data.comment);
                }
                ann = ann->next;
            }
        }

        appendf(ds, "      %-15s ", cur_ins->info->name);
        if (cur_ins->info->opcode == MVM_SSA_PHI) {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                MVMint16 orig = cur_ins->operands[i].reg.orig;
                MVMint16 regi = cur_ins->operands[i].reg.i;
                if (i)
                    append(ds, ", ");
                if (orig < 10) append(ds, " ");
                if (regi < 10) append(ds, " ");
                appendf(ds, "r%d(%d)", orig, regi);
            }
        }
        else {
            /* Count the opcode itself */
            size += 2;
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    append(ds, ", ");
                switch (cur_ins->info->operands[i] & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg: {
                        MVMint16 orig = cur_ins->operands[i].reg.orig;
                        MVMint16 regi = cur_ins->operands[i].reg.i;
                        if (orig < 10) append(ds, " ");
                        if (regi < 10) append(ds, " ");
                        appendf(ds, "r%d(%d)", orig, regi);
                        size += 4;
                        break;
                    }
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex: {
                        MVMStaticFrameBody *cursor = &g->sf->body;
                        MVMuint32 ascension;
                        appendf(ds, "lex(idx=%d,outers=%d", cur_ins->operands[i].lex.idx,
                            cur_ins->operands[i].lex.outers);
                        for (ascension = 0;
                                ascension < cur_ins->operands[i].lex.outers;
                                ascension++, cursor = &cursor->outer->body) { };
                        if (cursor->fully_deserialized) {
                            if (cur_ins->operands[i].lex.idx < cursor->num_lexicals) {
                                char *cstr = MVM_string_utf8_encode_C_string(tc, cursor->lexical_names_list[cur_ins->operands[i].lex.idx]);
                                appendf(ds, ",%s)", cstr);
                                MVM_free(cstr);
                            } else {
                                append(ds, ",<out of bounds>)");
                            }
                        } else {
                            append(ds, ",<pending deserialization>)");
                        }
                        size += 4;
                        break;
                    }
                    case MVM_operand_literal: {
                        MVMuint32 type = cur_ins->info->operands[i] & MVM_operand_type_mask;
                        switch (type) {
                        case MVM_operand_ins: {
                            MVMint32 bb_idx = cur_ins->operands[i].ins_bb->idx;
                            if (bb_idx < 100) append(ds, " ");
                            if (bb_idx < 10)  append(ds, " ");
                            appendf(ds, "BB(%d)", bb_idx);
                            size += 4;
                            break;
                        }
                        case MVM_operand_int8:
                            appendf(ds, "liti8(%"PRId8")", cur_ins->operands[i].lit_i8);
                            size += 2;
                            break;
                        case MVM_operand_int16:
                            appendf(ds, "liti16(%"PRId16")", cur_ins->operands[i].lit_i16);
                            size += 2;
                            break;
                        case MVM_operand_int32:
                            appendf(ds, "liti32(%"PRId32")", cur_ins->operands[i].lit_i32);
                            size += 4;
                            break;
                        case MVM_operand_uint32:
                            appendf(ds, "litui32(%"PRIu32")", cur_ins->operands[i].lit_ui32);
                            size += 4;
                            break;
                        case MVM_operand_int64:
                            appendf(ds, "liti64(%"PRId64")", cur_ins->operands[i].lit_i64);
                            size += 8;
                            break;
                        case MVM_operand_num32:
                            appendf(ds, "litn32(%f)", cur_ins->operands[i].lit_n32);
                            size += 4;
                            break;
                        case MVM_operand_num64:
                            appendf(ds, "litn64(%g)", cur_ins->operands[i].lit_n64);
                            size += 8;
                            break;
                        case MVM_operand_str: {
                            char *cstr = MVM_string_utf8_encode_C_string(tc,
                                MVM_cu_string(tc, g->sf->body.cu, cur_ins->operands[i].lit_str_idx));
                            appendf(ds, "lits(%s)", cstr);
                            MVM_free(cstr);
                            size += 8;
                            break;
                        }
                        case MVM_operand_callsite: {
                            MVMCallsite *callsite = g->sf->body.cu->body.callsites[cur_ins->operands[i].callsite_idx];
                            appendf(ds, "callsite(%p, %d arg, %d pos, %s, %s)",
                                    callsite,
                                    callsite->arg_count, callsite->num_pos,
                                    callsite->has_flattening ? "flattening" : "nonflattening",
                                    callsite->is_interned ? "interned" : "noninterned");
                            size += 2;
                            break;

                        }
                        case MVM_operand_spesh_slot:
                            appendf(ds, "sslot(%"PRId16")", cur_ins->operands[i].lit_i16);
                            size += 2;
                            break;
                        case MVM_operand_coderef: {
                            MVMCodeBody *body = &((MVMCode*)g->sf->body.cu->body.coderefs[cur_ins->operands[i].coderef_idx])->body;
                            MVMBytecodeAnnotation *anno = MVM_bytecode_resolve_annotation(tc, &body->sf->body, 0);

                            append(ds, "coderef(");

                            if (anno) {
                                char *filestr = MVM_string_utf8_encode_C_string(tc,
                                    MVM_cu_string(tc, g->sf->body.cu, anno->filename_string_heap_index));
                                appendf(ds, "%s:%d%s)", filestr, anno->line_number, body->outer ? " (closure)" : "");
                                MVM_free(filestr);
                            } else {
                                append(ds, "??\?)");
                            }

                            size += 2;

                            MVM_free(anno);
                            break;
                        }
                        default:
                            append(ds, "<nyi(lit)>");
                        }
                        break;
                    }
                    default:
                        append(ds, "<nyi>");
                }
            }
            if (cur_ins->info->opcode == MVM_OP_wval || cur_ins->info->opcode == MVM_OP_wval_wide) {
                /* We can try to find out what the debug_name of this thing is. */
                MVMint16 dep = cur_ins->operands[1].lit_i16;
                MVMint64 idx;
                MVMCollectable *result = NULL;
                MVMSerializationContext *sc;
                char *debug_name = NULL;
                const char *repr_name = NULL;
                if (cur_ins->info->opcode == MVM_OP_wval) {
                    idx = cur_ins->operands[2].lit_i16;
                } else {
                    idx = cur_ins->operands[2].lit_i64;
                }
                sc = MVM_sc_get_sc(tc, g->sf->body.cu, dep);
                if (sc)
                    result = (MVMCollectable *)MVM_sc_try_get_object(tc, sc, idx);
                if (result) {
                    if (result->flags1 & MVM_CF_STABLE) {
                        debug_name = MVM_6model_get_stable_debug_name(tc, (MVMSTable *)result);
                        repr_name  = ((MVMSTable *)result)->REPR->name;
                    } else {
                        debug_name = MVM_6model_get_debug_name(tc, (MVMObject *)result);
                        repr_name  = REPR(result)->name;
                    }
                    if (debug_name) {
                        appendf(ds, " (%s: %s)", repr_name, debug_name);
                    } else {
                        appendf(ds, " (%s: ?)", repr_name);
                    }
                } else {
                    appendf(ds, " (not deserialized)");
                }
            }
        }
        if (num_comments == 1) {
            ann = cur_ins->annotations;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_COMMENT) {
                    appendf(ds, "  # [%03d] %s", ann->order, ann->data.comment);
                    break;
                }
                ann = ann->next;
            }
        }
        append(ds, "\n");
        cur_ins = cur_ins->next;
    }

    if (stats) {
        if (bb->inlined)
            stats->inlined_size += size;
        stats->total_size += size;
    }

    /* Predecessors and successors. */
    append(ds, "    Successors: ");
    for (i = 0; i < bb->num_succ; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->succ[i]->idx);
    append(ds, "\n    Predecessors: ");
    for (i = 0; i < bb->num_pred; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->pred[i]->idx);
    append(ds, "\n    Dominance children: ");
    for (i = 0; i < bb->num_children; i++)
        appendf(ds, (i == 0 ? "%d" : ", %d"), bb->children[i]->idx);
    append(ds, "\n\n");
}

/* Dump deopt usages. */
static void dump_deopt_usages(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g, MVMSpeshOperand operand) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, operand);
    MVMSpeshDeoptUseEntry *entry = facts->usage.deopt_users;
    if (entry) {
        MVMuint32 first = 1;
        append(ds, ", deopt=");
        while (entry) {
            if (first)
                first = 0;
            else
                append(ds, ",");
            appendf(ds, "%d", entry->deopt_idx);
            entry = entry->next;
        }
    }
}
/* Dumps the facts table. */
static void dump_facts(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i, j, num_locals, num_facts;
    num_locals = g->num_locals;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 flags = g->facts[i][j].flags;
            MVMSpeshOperand operand;
            operand.reg.orig = i;
            operand.reg.i = j;
            if (i < 10) append(ds, " ");
            if (j < 10) append(ds, " ");
            if (flags || g->facts[i][j].dead_writer || (g->facts[i][j].writer && g->facts[i][j].writer->info->opcode == MVM_SSA_PHI)) {
                appendf(ds, "    r%d(%d): usages=%d%s", i, j,
                    MVM_spesh_usages_count(tc, g, operand),
                    MVM_spesh_usages_is_used_by_handler(tc, g, operand) ? "+handler" : "");
                dump_deopt_usages(tc, ds, g, operand);
                appendf(ds, ", flags=%-5d", flags);
                if (flags & 1) {
                    append(ds, " KnTyp");
                }
                if (flags & 2) {
                    append(ds, " KnVal");
                }
                if (flags & 4) {
                    append(ds, " Dcntd");
                }
                if (flags & 8) {
                    append(ds, " Concr");
                }
                if (flags & 16) {
                    append(ds, " TyObj");
                }
                if (flags & 32) {
                    append(ds, " KnDcT");
                }
                if (flags & 64) {
                    append(ds, " DCncr");
                }
                if (flags & 128) {
                    append(ds, " DcTyO");
                }
                if (flags & 256) {
                    append(ds, " LogGd");
                }
                if (flags & 512) {
                    append(ds, " HashI");
                }
                if (flags & 1024) {
                    append(ds, " ArrIt");
                }
                if (flags & 2048) {
                    append(ds, " KBxSr");
                }
                if (flags & 4096) {
                    append(ds, " MgWLG");
                }
                if (flags & 8192) {
                    append(ds, " KRWCn");
                }
                if (g->facts[i][j].dead_writer) {
                    append(ds, " DeadWriter");
                }
                if (g->facts[i][j].writer && g->facts[i][j].writer->info->opcode == MVM_SSA_PHI) {
                    appendf(ds, " (merged from %d regs)", g->facts[i][j].writer->info->num_operands - 1);
                }
                if (flags & 1) {
                    appendf(ds, " (type: %s)", MVM_6model_get_debug_name(tc, g->facts[i][j].type));
                }
            }
            else {
                appendf(ds, "    r%d(%d): usages=%d%s", i, j,
                    MVM_spesh_usages_count(tc, g, operand),
                    MVM_spesh_usages_is_used_by_handler(tc, g, operand) ? "+handler" : "");
                dump_deopt_usages(tc, ds, g, operand);
                appendf(ds, ", flags=%-5d", flags);
            }
            append(ds, "\n");
        }
        append(ds, "\n");
    }
}

static void dump_callsite(MVMThreadContext *tc, DumpStr *ds, MVMCallsite *cs) {
    MVMuint16 i;
    appendf(ds, "Callsite %p (%d args, %d pos)\n", cs, cs->arg_count, cs->num_pos);
    for (i = 0; i < (cs->arg_count - cs->num_pos) / 2; i++) {
        if (cs->arg_names[i]) {
            char * argname_utf8 = MVM_string_utf8_encode_C_string(tc, cs->arg_names[i]);
            appendf(ds, "  - %s\n", argname_utf8);
            MVM_free(argname_utf8);
        }
    }
    if (cs->num_pos)
        append(ds, "Positional flags: ");
    for (i = 0; i < cs->num_pos; i++) {
        MVMCallsiteEntry arg_flag = cs->arg_flags[i];

        if (i)
            append(ds, ", ");

        if (arg_flag == MVM_CALLSITE_ARG_OBJ) {
            append(ds, "obj");
        } else if (arg_flag == MVM_CALLSITE_ARG_INT) {
            append(ds, "int");
        } else if (arg_flag == MVM_CALLSITE_ARG_NUM) {
            append(ds, "num");
        } else if (arg_flag == MVM_CALLSITE_ARG_STR) {
            append(ds, "str");
        }
    }
    if (cs->num_pos)
        append(ds, "\n");
    append(ds, "\n");
}

static void dump_fileinfo(MVMThreadContext *tc, DumpStr *ds, MVMStaticFrame *sf) {
    MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &sf->body, 0);
    MVMCompUnit            *cu = sf->body.cu;
    MVMuint32          str_idx = ann ? ann->filename_string_heap_index : 0;
    MVMint32           line_nr = ann ? ann->line_number : 1;
    MVMString        *filename = cu->body.filename;
    char        *filename_utf8 = "<unknown>";
    if (ann && str_idx < cu->body.num_strings) {
        filename = MVM_cu_string(tc, cu, str_idx);
    }
    if (filename)
        filename_utf8 = MVM_string_utf8_encode_C_string(tc, filename);
    appendf(ds, "%s:%d", filename_utf8, line_nr);
    if (filename)
        MVM_free(filename_utf8);
    MVM_free(ann);
}

static void dump_deopt_pea(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint32 i, j;
    if (MVM_VECTOR_ELEMS(g->deopt_pea.materialize_info)) {
        append(ds, "\nMaterializations:\n");
        for (i = 0; i < MVM_VECTOR_ELEMS(g->deopt_pea.materialize_info); i++) {
            MVMSpeshPEAMaterializeInfo *mat = &(g->deopt_pea.materialize_info[i]);
            MVMSTable *st = (MVMSTable *)g->spesh_slots[mat->stable_sslot];
            appendf(ds, "  %d: %s from regs ", i, st->debug_name);
            for (j = 0; j < mat->num_attr_regs; j++)
                appendf(ds, j > 0 ? ", r%hu" : "r%hu", mat->attr_regs[j]);
            append(ds, "\n");
        }
    }
    if (MVM_VECTOR_ELEMS(g->deopt_pea.deopt_point)) {
        append(ds, "\nDeopt point materialization mappings:\n");
        for (i = 0; i < MVM_VECTOR_ELEMS(g->deopt_pea.deopt_point); i++) {
            MVMSpeshPEADeoptPoint *dp = &(g->deopt_pea.deopt_point[i]);
            appendf(ds, "  At %d materialize %d into r%d\n", dp->deopt_point_idx,
                    dp->materialize_info_idx, dp->target_reg);
        }
    }
}

/* Dump a spesh graph into string form, for debugging purposes. */
char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *cur_bb;
    SpeshGraphSizeStats stats;
    InlineIndexStack inline_stack;
    DumpStr ds;

    stats.total_size = 0;
    stats.inlined_size = 0;
    inline_stack.cur_depth = -1;

    /* Allocate buffer. */
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    append(&ds, "Spesh of '");
    append_str(tc, &ds, g->sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, g->sf->body.cuuid);
    append(&ds, ", file: ");
    dump_fileinfo(tc, &ds, g->sf);
    append(&ds, ")\n");
    if (g->cs)
        dump_callsite(tc, &ds, g->cs);
    if (!g->cs)
        append(&ds, "\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, &ds, g, cur_bb, &stats, &inline_stack);
        cur_bb = cur_bb->linear_next;
    }

    /* Dump facts. */
    if (g->facts) {
        append(&ds, "\nFacts:\n");
        dump_facts(tc, &ds, g);
    }

    /* Dump spesh slots. */
    if (g->num_spesh_slots) {
        MVMuint32 i;
        append(&ds, "\nSpesh slots:\n");
        for (i = 0; i < g->num_spesh_slots; i++) {
            MVMCollectable *value = g->spesh_slots[i];
            if (value == NULL)
                appendf(&ds, "    %d = NULL\n", i);
            else if (value->flags1 & MVM_CF_STABLE)
                appendf(&ds, "    %d = STable (%s)\n", i,
                    MVM_6model_get_stable_debug_name(tc, (MVMSTable *)value));
            else if (value->flags1 & MVM_CF_TYPE_OBJECT)
                appendf(&ds, "    %d = Type Object (%s)\n", i,
                    MVM_6model_get_debug_name(tc, (MVMObject *)value));
            else {
                MVMObject *obj = (MVMObject *)value;
                MVMuint32 repr_id = REPR(obj)->ID;
                appendf(&ds, "    %d = Instance (%s)", i,
                    MVM_6model_get_debug_name(tc, obj));
                if (repr_id == MVM_REPR_ID_MVMStaticFrame || repr_id == MVM_REPR_ID_MVMCode) {
                    MVMStaticFrameBody *body;
                    char *name_str;
                    char *cuuid_str;
                    if (repr_id == MVM_REPR_ID_MVMCode) {
                        MVMCodeBody *code_body = (MVMCodeBody *)OBJECT_BODY(obj);
                        obj = (MVMObject *)code_body->sf;
                    }
                    body = (MVMStaticFrameBody *)OBJECT_BODY(obj);
                    name_str  = MVM_string_utf8_encode_C_string(tc, body->name);
                    cuuid_str = MVM_string_utf8_encode_C_string(tc, body->cuuid);
                    appendf(&ds, " - '%s' (%s)", name_str, cuuid_str);
                    MVM_free(name_str);
                    MVM_free(cuuid_str);
                }
                appendf(&ds, "\n");
            }
        }
    }

    /* Dump materialization deopt into. */
    dump_deopt_pea(tc, &ds, g);

    append(&ds, "\n");

    /* Print out frame size */
    if (stats.inlined_size)
        appendf(&ds, "Frame size: %u bytes (%u from inlined frames)\n", stats.total_size, stats.inlined_size);
    else
        appendf(&ds, "Frame size: %u bytes\n", stats.total_size);

    append_null(&ds);
    return ds.buffer;
}

/* Dumps a spesh stats type typle. */
void dump_stats_type_tuple(MVMThreadContext *tc, DumpStr *ds, MVMCallsite *cs,
                           MVMSpeshStatsType *type_tuple, char *prefix) {
    MVMuint32 j;
    for (j = 0; j < cs->flag_count; j++) {
        MVMObject *type = type_tuple[j].type;
        if (type) {
            MVMObject *decont_type = type_tuple[j].decont_type;
            appendf(ds, "%sType %d: %s%s (%s)",
                prefix, j,
                (type_tuple[j].rw_cont ? "RW " : ""),
                MVM_6model_get_stable_debug_name(tc, type->st),
                (type_tuple[j].type_concrete ? "Conc" : "TypeObj"));
            if (decont_type)
                appendf(ds, " of %s (%s)",
                    MVM_6model_get_stable_debug_name(tc, decont_type->st),
                    (type_tuple[j].decont_type_concrete ? "Conc" : "TypeObj"));
            append(ds, "\n");
        }
    }
}

/* Dumps the statistics associated with a particular callsite object. */
void dump_stats_by_callsite(MVMThreadContext *tc, DumpStr *ds, MVMSpeshStatsByCallsite *css) {
    MVMuint32 i, j, k;

    if (css->cs)
        dump_callsite(tc, ds, css->cs);
    else
        append(ds, "No interned callsite\n");
    appendf(ds, "    Callsite hits: %d\n\n", css->hits);
    if (css->osr_hits)
        appendf(ds, "    OSR hits: %d\n\n", css->osr_hits);
    appendf(ds, "    Maximum stack depth: %d\n\n", css->max_depth);

    for (i = 0; i < css->num_by_type; i++) {
        MVMSpeshStatsByType *tss = &(css->by_type[i]);
        appendf(ds, "    Type tuple %d\n", i);
        dump_stats_type_tuple(tc, ds, css->cs, tss->arg_types, "        ");
        appendf(ds, "        Hits: %d\n", tss->hits);
        if (tss->osr_hits)
            appendf(ds, "        OSR hits: %d\n", tss->osr_hits);
        appendf(ds, "        Maximum stack depth: %d\n", tss->max_depth);
        if (tss->num_by_offset) {
            append(ds, "        Logged at offset:\n");
            for (j = 0; j < tss->num_by_offset; j++) {
                MVMSpeshStatsByOffset *oss = &(tss->by_offset[j]);
                appendf(ds, "            %d:\n", oss->bytecode_offset);
                for (k = 0; k < oss->num_types; k++)
                    appendf(ds, "                %d x type %s (%s)\n",
                        oss->types[k].count,
                        MVM_6model_get_stable_debug_name(tc, oss->types[k].type->st),
                        (oss->types[k].type_concrete ? "Conc" : "TypeObj"));
                for (k = 0; k < oss->num_invokes; k++) {
                    char *body_name = MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.name);
                    char *body_cuuid = MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.cuuid);
                    appendf(ds,
                        "                %d x static frame '%s' (%s) (caller is outer: %d, multi %d)\n",
                        oss->invokes[k].count,
                        body_name,
                        body_cuuid,
                        oss->invokes[k].caller_is_outer_count,
                        oss->invokes[k].was_multi_count);
                    MVM_free(body_name);
                    MVM_free(body_cuuid);
                }
                for (k = 0; k < oss->num_type_tuples; k++) {
                    appendf(ds, "                %d x type tuple:\n",
                        oss->type_tuples[k].count);
                    dump_stats_type_tuple(tc, ds, oss->type_tuples[k].cs,
                        oss->type_tuples[k].arg_types,
                        "                    ");
                }
                for (k = 0; k < oss->num_plugin_guards; k++)
                    appendf(ds, "                %d x spesh plugin guard index %d\n",
                        oss->plugin_guards[k].count,
                        oss->plugin_guards[k].guard_index);
            }
        }
        append(ds, "\n");
    }
}

/* Dumps the statistics associated with a static frame into a string. */
char * MVM_spesh_dump_stats(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshStats *ss = sf->body.spesh->body.spesh_stats;

    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    append(&ds, "Latest statistics for '");
    append_str(tc, &ds, sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, sf->body.cuuid);
    append(&ds, ", file: ");
    dump_fileinfo(tc, &ds, sf);
    append(&ds, ")\n\n");

    /* Dump the spesh stats if present. */
    if (ss) {
        MVMuint32 i;

        appendf(&ds, "Total hits: %d\n", ss->hits);
        if (ss->osr_hits)
            appendf(&ds, "OSR hits: %d\n", ss->osr_hits);
        append(&ds, "\n");

        for (i = 0; i < ss->num_by_callsite; i++)
            dump_stats_by_callsite(tc, &ds, &(ss->by_callsite[i]));
    }
    else {
        append(&ds, "No spesh stats for this static frame\n");
    }

    append(&ds, "\n");
    append_null(&ds);
    return ds.buffer;
}

/* Dumps a planned specialization into a string. */
char * MVM_spesh_dump_planned(MVMThreadContext *tc, MVMSpeshPlanned *p) {
    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump kind of specialization and target. */
    switch (p->kind) {
        case MVM_SPESH_PLANNED_CERTAIN:
            append(&ds, "Certain");
            break;
        case MVM_SPESH_PLANNED_OBSERVED_TYPES:
            append(&ds, "Observed type");
            break;
        case MVM_SPESH_PLANNED_DERIVED_TYPES:
            append(&ds, "Derived type");
            break;
    }
    append(&ds, " specialization of '");
    append_str(tc, &ds, p->sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, p->sf->body.cuuid);
    append(&ds, ", file: ");
    dump_fileinfo(tc, &ds, p->sf);
    append(&ds, ")\n\n");

    /* Dump the callsite of the specialization. */
    if (p->cs_stats->cs) {
        append(&ds, "The specialization is for the callsite:\n");
        dump_callsite(tc, &ds, p->cs_stats->cs);
    }
    else {
        append(&ds, "The specialization is for when there is no interned callsite.\n");
    }

    /* Dump reasoning. */
    switch (p->kind) {
        case MVM_SPESH_PLANNED_CERTAIN:
            if (p->cs_stats->hits >= MVM_spesh_threshold(tc, p->sf))
                appendf(&ds,
                    "It was planned due to the callsite receiving %u hits.\n",
                    p->cs_stats->hits);
            else if (p->cs_stats->osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
                appendf(&ds,
                    "It was planned due to the callsite receiving %u OSR hits.\n",
                    p->cs_stats->osr_hits);
            else
                append(&ds, "It was planned for unknown reasons.\n");
            if (!p->sf->body.specializable)
                append(&ds, "The body contains no specializable instructions.\n");
            break;
        case MVM_SPESH_PLANNED_OBSERVED_TYPES: {
            MVMCallsite *cs = p->cs_stats->cs;
            MVMuint32 hit_percent = p->cs_stats->hits
               ? (100 * p->type_stats[0]->hits) / p->cs_stats->hits
               : 0;
            MVMuint32 osr_hit_percent = p->cs_stats->osr_hits
               ? (100 * p->type_stats[0]->osr_hits) / p->cs_stats->osr_hits
               : 0;
            append(&ds, "It was planned for the type tuple:\n");
            dump_stats_type_tuple(tc, &ds, cs, p->type_tuple, "    ");
            if (osr_hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT_OSR)
                appendf(&ds, "Which received %u OSR hits (%u%% of the %u callsite OSR hits).\n",
                    p->type_stats[0]->osr_hits, osr_hit_percent, p->cs_stats->osr_hits);
            else if (hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT)
                appendf(&ds, "Which received %u hits (%u%% of the %u callsite hits).\n",
                    p->type_stats[0]->hits, hit_percent, p->cs_stats->hits);
            else
                append(&ds, "For unknown reasons.\n");
            break;
        }
        case MVM_SPESH_PLANNED_DERIVED_TYPES: {
            MVMCallsite *cs = p->cs_stats->cs;
            append(&ds, "It was planned for the type tuple:\n");
            dump_stats_type_tuple(tc, &ds, cs, p->type_tuple, "    ");
            break;
        }
    }

    appendf(&ds, "\nThe maximum stack depth is %d.\n\n", p->max_depth);
    append_null(&ds);
    return ds.buffer;
}

/* Dumps a static frame's guard set into a string. */
char * MVM_spesh_dump_arg_guard(MVMThreadContext *tc, MVMStaticFrame *sf, MVMSpeshArgGuard *ag) {
    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    if (sf) {
        append(&ds, "Latest guard tree for '");
        append_str(tc, &ds, sf->body.name);
        append(&ds, "' (cuid: ");
        append_str(tc, &ds, sf->body.cuuid);
        append(&ds, ", file: ");
        dump_fileinfo(tc, &ds, sf);
        append(&ds, ")\n\n");
    }

    /* Dump nodes. */
    if (ag) {
        MVMuint32 i = 0;
        for (i = 0; i < ag->used_nodes; i++) {
            MVMSpeshArgGuardNode *agn = &(ag->nodes[i]);
            switch (agn->op) {
                case MVM_SPESH_GUARD_OP_CALLSITE:
                    appendf(&ds, "%u: CALLSITE %p | Y: %u, N: %u\n",
                        i, agn->cs, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_LOAD_ARG:
                    appendf(&ds, "%u: LOAD ARG %d | Y: %u\n",
                        i, agn->arg_index, agn->yes);
                    break;
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                    appendf(&ds, "%u: STABLE CONC %s | Y: %u, N: %u\n",
                        i, MVM_6model_get_stable_debug_name(tc, agn->st), agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    appendf(&ds, "%u: STABLE CONC %s | Y: %u, N: %u\n",
                        i, MVM_6model_get_stable_debug_name(tc, agn->st), agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_VALUE:
                    appendf(&ds, "%u: DEREF_VALUE %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_RW:
                    appendf(&ds, "%u: DEREF_RW %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_RESULT:
                    appendf(&ds, "%u: RESULT %u\n", i, agn->result);
                    break;
            }
        }
    }
    else {
        append(&ds, "No argument guard nodes\n");
    }

    append(&ds, "\n");
    append_null(&ds);
    return ds.buffer;
}
