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

/* Dumps a basic block. */
static void dump_bb(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g, MVMSpeshBB *bb) {
    MVMSpeshIns *cur_ins;
    MVMint64     i;

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
        MVMuint32 line_number;

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
                    break;
                case MVM_SPESH_ANN_INLINE_END:
                    appendf(ds, "      [Annotation: Inline End (%d)]\n",
                        ann->data.inline_idx);
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
                    char *cstr = MVM_string_utf8_encode_C_string(tc,
                        MVM_cu_string(tc, g->sf->body.cu, ann->data.lineno.filename_string_index));
                    appendf(ds, "      [Annotation: Line Number: %s:%d]\n",
                        cstr, ann->data.lineno.line_number);
                    MVM_free(cstr);
                    break;
                }
                case MVM_SPESH_ANN_LOGGED:
                    appendf(ds, "      [Annotation: Logged (bytecode offset %d]\n",
                        ann->data.bytecode_offset);
                    break;
                default:
                    appendf(ds, "      [Annotation: %d (unknown)]\n", ann->type);
            }
            ann = ann->next;
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
                                char *cstr = MVM_string_utf8_encode_C_string(tc, cursor->lexical_names_list[cur_ins->operands[i].lex.idx]->key);
                                appendf(ds, ",%s)", cstr);
                                MVM_free(cstr);
                            } else {
                                append(ds, ",<out of bounds>)");
                            }
                        } else {
                            append(ds, ",<pending deserialization>)");
                        }
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
                            break;
                        }
                        case MVM_operand_int8:
                            appendf(ds, "liti8(%"PRId8")", cur_ins->operands[i].lit_i8);
                            break;
                        case MVM_operand_int16:
                            appendf(ds, "liti16(%"PRId16")", cur_ins->operands[i].lit_i16);
                            break;
                        case MVM_operand_int32:
                            appendf(ds, "liti32(%"PRId32")", cur_ins->operands[i].lit_i32);
                            break;
                        case MVM_operand_uint32:
                            appendf(ds, "litui32(%"PRIu32")", cur_ins->operands[i].lit_ui32);
                            break;
                        case MVM_operand_int64:
                            appendf(ds, "liti64(%"PRId64")", cur_ins->operands[i].lit_i64);
                            break;
                        case MVM_operand_num32:
                            appendf(ds, "litn32(%f)", cur_ins->operands[i].lit_n32);
                            break;
                        case MVM_operand_num64:
                            appendf(ds, "litn64(%g)", cur_ins->operands[i].lit_n64);
                            break;
                        case MVM_operand_str: {
                            char *cstr = MVM_string_utf8_encode_C_string(tc,
                                MVM_cu_string(tc, g->sf->body.cu, cur_ins->operands[i].lit_str_idx));
                            appendf(ds, "lits(%s)", cstr);
                            MVM_free(cstr);
                            break;
                        }
                        case MVM_operand_callsite: {
                            MVMCallsite *callsite = g->sf->body.cu->body.callsites[cur_ins->operands[i].callsite_idx];
                            appendf(ds, "callsite(%p, %d arg, %d pos, %s, %s)",
                                    callsite,
                                    callsite->arg_count, callsite->num_pos,
                                    callsite->has_flattening ? "flattening" : "nonflattening",
                                    callsite->is_interned ? "interned" : "noninterned");
                            break;

                        }
                        case MVM_operand_spesh_slot:
                            appendf(ds, "sslot(%"PRId16")", cur_ins->operands[i].lit_i16);
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
                    if (result->flags & MVM_CF_STABLE) {
                        debug_name = ((MVMSTable *)result)->debug_name;
                        repr_name  = ((MVMSTable *)result)->REPR->name;
                    } else {
                        debug_name = STABLE(result)->debug_name;
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
        append(ds, "\n");
        cur_ins = cur_ins->next;
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

/* Dumps the facts table. */
static void dump_facts(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i, j, num_locals, num_facts;
    num_locals = g->num_locals;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 usages = g->facts[i][j].usages;
            MVMint32 flags  = g->facts[i][j].flags;
            if (i < 10) append(ds, " ");
            if (j < 10) append(ds, " ");
            if (flags || g->facts[i][j].dead_writer || g->facts[i][j].writer && g->facts[i][j].writer->info->opcode == MVM_SSA_PHI) {
                appendf(ds, "    r%d(%d): usages=%d, flags=%-5d", i, j, usages, flags);
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
            }
            else
                appendf(ds, "    r%d(%d): usages=%d, flags=%d", i, j, usages, flags);
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
    MVMint32           str_idx = ann ? ann->filename_string_heap_index : 0;
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
    dump_fileinfo(tc, &ds, g->sf);
    append(&ds, ")\n");
    if (g->cs)
        dump_callsite(tc, &ds, g->cs);
    if (!g->cs)
        append(&ds, "\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, &ds, g, cur_bb);
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
            else if (value->flags & MVM_CF_STABLE)
                appendf(&ds, "    %d = STable (%s)\n", i,
                    ((MVMSTable *)value)->debug_name);
            else if (value->flags & MVM_CF_TYPE_OBJECT)
                appendf(&ds, "    %d = Type Object (%s)\n", i,
                    ((MVMObject *)value)->st->debug_name);
            else
                appendf(&ds, "    %d = Instance (%s)\n", i,
                    ((MVMObject *)value)->st->debug_name);
        }
    }

    append(&ds, "\n");
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
                type->st->debug_name,
                (type_tuple[j].type_concrete ? "Conc" : "TypeObj"));
            if (decont_type)
                appendf(ds, " of %s (%s)",
                    decont_type->st->debug_name,
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
                        oss->types[k].type->st->debug_name,
                        (oss->types[k].type_concrete ? "Conc" : "TypeObj"));
                for (k = 0; k < oss->num_invokes; k++)
                    appendf(ds,
                        "                %d x static frame '%s' (%s) (caller is outer: %d, multi %d)\n",
                        oss->invokes[k].count,
                        MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.name),
                        MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.cuuid),
                        oss->invokes[k].caller_is_outer_count,
                        oss->invokes[k].was_multi_count);
                for (k = 0; k < oss->num_type_tuples; k++) {
                    appendf(ds, "                %d x type tuple:\n",
                        oss->type_tuples[k].count);
                    dump_stats_type_tuple(tc, ds, oss->type_tuples[k].cs,
                        oss->type_tuples[k].arg_types,
                        "                    ");
                }
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

        if (ss->num_static_values) {
            append(&ds, "Static values:\n");
            for (i = 0; i < ss->num_static_values; i++)
                appendf(&ds, "    - %s (%p) @ %d\n",
                    ss->static_values[i].value->st->debug_name,
                    ss->static_values[i].value,
                    ss->static_values[i].bytecode_offset);
        }
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
        case MVM_SPESH_PLANNED_DERIVED_TYPES:
            break;
    }

    appendf(&ds, "\nThe maximum stack depth is %d.\n\n", p->max_depth);
    append_null(&ds);
    return ds.buffer;
}

/* Dumps a static frame's guard set into a string. */
char * MVM_spesh_dump_arg_guard(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshArgGuard *ag = sf->body.spesh->body.spesh_arg_guard;

    DumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    /* Dump name and CUID. */
    append(&ds, "Latest guard tree for '");
    append_str(tc, &ds, sf->body.name);
    append(&ds, "' (cuid: ");
    append_str(tc, &ds, sf->body.cuuid);
    append(&ds, ", file: ");
    dump_fileinfo(tc, &ds, sf);
    append(&ds, ")\n\n");

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
                        i, agn->st->debug_name, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    appendf(&ds, "%u: STABLE CONC %s | Y: %u, N: %u\n",
                        i, agn->st->debug_name, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_VALUE:
                    appendf(&ds, "%u: DEREF_VALUE %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_RW:
                    appendf(&ds, "%u: DEREF_RW %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_CERTAIN_RESULT:
                    appendf(&ds, "%u: CERTAIN RESULT %u\n", i, agn->result);
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
