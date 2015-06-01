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
    c_message[vsnprintf(c_message, 1023, fmt, args)] = 0;
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
                        case MVM_operand_ins:
                            appendf(ds, "BB(%d)", cur_ins->operands[i].ins_bb->idx);
                            break;
                        case MVM_operand_int8:
                            appendf(ds, "liti8(%"PRId8")", cur_ins->operands[i].lit_i8);
                            break;
                        case MVM_operand_int16:
                            appendf(ds, "liti16(%"PRId16")", cur_ins->operands[i].lit_i16);
                            break;
                        case MVM_operand_int32:
                            appendf(ds, "liti32(%"PRId32")", cur_ins->operands[i].lit_i32);
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
                            char *cstr = MVM_string_utf8_encode_C_string(tc, g->sf->body.cu->body.strings[cur_ins->operands[i].lit_str_idx]);
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
                            MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &body->sf->body, 0);

                            append(ds, "coderef(");

                            if (ann) {
                                char *filestr = MVM_string_utf8_encode_C_string(tc, g->sf->body.cu->body.strings[ann->filename_string_heap_index]);
                                appendf(ds, "%s:%d%s)", filestr, ann->line_number, body->outer ? " (closure)" : "");
                                MVM_free(filestr);
                            } else {
                                append(ds, "??\?)");
                            }

                            MVM_free(ann);
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
            appendf(ds, "    r%d(%d): usages=%d, flags=%d", i, j, usages, flags);
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
            append(ds, "\n");
        }
    }
}

/* Dumps a table of all logged values */
static void dump_log_values(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMint16 log_index;
    MVMint16 seen_table_size =  g->num_log_slots * MVM_SPESH_LOG_RUNS;
    size_t   ds_pos_before = tell_ds(ds);
    MVMint16 interesting = 0;

    MVMCollectable **seen_table = alloca(sizeof(MVMCollectable *) *seen_table_size);
    memset(seen_table, 0, sizeof(MVMCollectable *) * seen_table_size);

    append(ds, "Logged values:\n");

    for (log_index = 0; log_index < g->num_log_slots; log_index++) {
        MVMint16 run_index;

        appendf(ds, "    % 3d ", log_index);

        for (run_index = 0; run_index < MVM_SPESH_LOG_RUNS; run_index++) {
            MVMuint16       log_slot = log_index * MVM_SPESH_LOG_RUNS + run_index;
            MVMCollectable *log_obj  = g->log_slots[log_slot];
            MVMint16        log_obj_idx;

            if (log_obj) {
                for (log_obj_idx = 0; log_obj_idx < seen_table_size; log_obj_idx++) {
                    if (seen_table[log_obj_idx] == log_obj) {
                        break;
                    } else if (seen_table[log_obj_idx] == 0) {
                        seen_table[log_obj_idx] = log_obj;
                        break;
                    }
                }

                appendf(ds, "% 4d  ", log_obj_idx + 1);
                interesting = 1;
            } else {
                appendf(ds, "% 4s  ", "_");
            }

        }

        append(ds, "\n");
    }
    append(ds, "\n");

    for (log_index = 0; log_index < seen_table_size && seen_table[log_index]; log_index++) {
        appendf(ds, "    %d: %p", log_index + 1, seen_table[log_index]);
        if (STABLE(seen_table[log_index])->REPR->ID == MVM_REPR_ID_P6int) {
            if (IS_CONCRETE(seen_table[log_index]))
                appendf(ds, " P6int(%d)", MVM_repr_get_int(tc, (MVMObject*)seen_table[log_index]));
            else
                append(ds, " P6int(type object)");
        } else {
            append(ds, " ");
            append(ds, STABLE(seen_table[log_index])->REPR->name);
            if (!IS_CONCRETE(seen_table[log_index]))
                append(ds, "(type object)");
        }
        append(ds, "\n");
    }

    append(ds, "\n");

    if (!interesting) {
        rewind_ds(ds, ds_pos_before);
    }
}

static void dump_callsite(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i;
    appendf(ds, "Callsite %p (%d args, %d pos)\n", g->cs, g->cs->arg_count, g->cs->num_pos);
    for (i = 0; i < (g->cs->arg_count - g->cs->num_pos) / 2; i++) {
        if (g->cs->arg_names[i]) {
            char * argname_utf8 = MVM_string_utf8_encode_C_string(tc, g->cs->arg_names[i]);
            appendf(ds, "  - %s\n", argname_utf8);
            MVM_free(argname_utf8);
        }
    }
    append(ds, "\n");
}

static void dump_arg_guards(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i;
    appendf(ds, "%d argument guards\n", g->num_arg_guards);

    for (i = 0; i < g->num_arg_guards; i++) {
        MVMSpeshGuard *guard = &g->arg_guards[i];
        switch (guard->kind) {
        case MVM_SPESH_GUARD_CONC:
            appendf(ds, "  concrete(%d)\n", guard->slot);
            break;
        case MVM_SPESH_GUARD_TYPE:
            appendf(ds, "  type(%d, %p)\n", guard->slot, guard->match);
            break;
        case MVM_SPESH_GUARD_DC_CONC:
            appendf(ds, "  deconted_concrete(%d)\n", guard->slot);
            break;
        case MVM_SPESH_GUARD_DC_TYPE:
            appendf(ds, "  deconted_type(%d, %p)\n", guard->slot, guard->match);
            break;
        }
    }
    append(ds, "\n");
}

static void dump_fileinfo(MVMThreadContext *tc, DumpStr *ds, MVMSpeshGraph *g) {
    MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &g->sf->body, 0);
    MVMCompUnit            *cu = g->sf->body.cu;
    MVMint32           str_idx = ann ? ann->filename_string_heap_index : 0;
    MVMint32           line_nr = ann ? ann->line_number : 1;
    MVMString        *filename = cu->body.filename;
    char        *filename_utf8 = "<unknown>";
    if (ann && str_idx < cu->body.num_strings) {
        filename = cu->body.strings[str_idx];
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
    dump_fileinfo(tc, &ds, g);
    append(&ds, ")\n");
    if (g->cs)
        dump_callsite(tc, &ds, g);
    if (g->num_arg_guards)
        dump_arg_guards(tc, &ds, g);
    if (!g->cs && !g->num_arg_guards)
        append(&ds, "\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, &ds, g, cur_bb);
        cur_bb = cur_bb->linear_next;
    }

    /* Dump facts. */
    append(&ds, "\nFacts:\n");
    dump_facts(tc, &ds, g);

    if (g->num_spesh_slots || g->num_log_slots) {
        append(&ds, "\nStats:\n");
        appendf(&ds, "    %d spesh slots\n", g->num_spesh_slots);
        appendf(&ds, "    %d log slots\n", g->num_log_slots);
    }

    if (g->num_log_slots) {
        dump_log_values(tc, &ds, g);
    }

    append(&ds, "\n");
    append_null(&ds);
    return ds.buffer;
}
