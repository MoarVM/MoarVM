#define MVM_INTERNAL_HELPERS
#include "moar.h"

void MVM_ds_append(MVMDumpStr *ds, char *to_add) {
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

size_t MVM_ds_tell(MVMDumpStr *ds) {
    return ds->pos;
}

void MVM_ds_rewind(MVMDumpStr *ds, size_t target) {
    if (ds->pos > target) {
        ds->pos = target;
        ds->buffer[ds->pos + 1] = '\0';
    }
}

/* Formats a string and then MVM_ds_appends it. */
MVM_FORMAT(printf, 2, 3)
void MVM_ds_appendf(MVMDumpStr *ds, const char *fmt, ...) {
    char c_message[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(c_message, 2047, fmt, args);
    MVM_ds_append(ds, c_message);
    va_end(args);
}

/* Turns a MoarVM string into a C string and MVM_ds_appends it. */
void MVM_ds_append_str(MVMThreadContext *tc, MVMDumpStr *ds, MVMString *s) {
    char *cs = MVM_string_utf8_encode_C_string(tc, s);
    MVM_ds_append(ds, cs);
    MVM_free(cs);
}

/* MVM_ds_appends a null at the end. */
void MVM_ds_append_null(MVMDumpStr *ds) {
    MVM_ds_append(ds, " "); /* Delegate realloc if we're really unlucky. */
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
static void dump_bb(MVMThreadContext *tc, MVMDumpStr *ds, MVMSpeshGraph *g, MVMSpeshBB *bb,
                    SpeshGraphSizeStats *stats, InlineIndexStack *inline_stack) {
    MVMSpeshIns *cur_ins;
    MVMint64     i;
    MVMint32     size = 0;

    /* Heading. */
    MVM_ds_appendf(ds, "  BB %d (%p):\n", bb->idx, bb);

    if (bb->inlined) {
        MVM_ds_append(ds, "    Inlined\n");
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
        MVM_ds_appendf(ds, "    line: %d (pc %d)\n", line_number, bb->initial_pc);
    }

    /* Instructions. */
    MVM_ds_append(ds, "    Instructions:\n");
    cur_ins = bb->first_ins;
    while (cur_ins) {
        MVMSpeshAnn *ann = cur_ins->annotations;
        MVMuint32 line_number = -1;
        MVMuint32 pop_inlines = 0;
        MVMuint32 num_comments = 0;

        while (ann) {
            /* These annotations carry a deopt index that we can find a
             * corresponding line number for */
            if (ann->type == MVM_SPESH_ANN_DEOPT_ONE_INS
                || ann->type == MVM_SPESH_ANN_DEOPT_PRE_INS
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
                    MVM_ds_appendf(ds, "      [Annotation: FH Start (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_END:
                    MVM_ds_appendf(ds, "      [Annotation: FH End (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_FH_GOTO:
                    MVM_ds_appendf(ds, "      [Annotation: FH Goto (%d)]\n",
                        ann->data.frame_handler_index);
                    break;
                case MVM_SPESH_ANN_DEOPT_ONE_INS:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt One After Instruction (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_DEOPT_PRE_INS:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt One Before Instruction (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_DEOPT_ALL_INS:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt All (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_INLINE_START:
                    MVM_ds_appendf(ds, "      [Annotation: Inline Start (%d)]\n",
                        ann->data.inline_idx);
                    push_inline(tc, inline_stack, ann->data.inline_idx);
                    break;
                case MVM_SPESH_ANN_INLINE_END:
                    MVM_ds_appendf(ds, "      [Annotation: Inline End (%d)]\n",
                        ann->data.inline_idx);
                    pop_inlines++;
                    break;
                case MVM_SPESH_ANN_DEOPT_INLINE:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt Inline (idx %d -> pc %d; line %d)]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_DEOPT_OSR:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt OSR (idx %d -> pc %d); line %d]\n",
                        ann->data.deopt_idx, g->deopt_addrs[2 * ann->data.deopt_idx], line_number);
                    break;
                case MVM_SPESH_ANN_LINENO: {
                    char *cstr;
                    MVMCompUnit *cu = get_current_cu(tc, g, inline_stack);
                    if (cu->body.num_strings < ann->data.lineno.filename_string_index) {
                        MVM_ds_appendf(ds, "      [Annotation: Line Number: <out of bounds>:%d]\n",
                        ann->data.lineno.line_number);
                    }
                    else {
                        cstr = MVM_string_utf8_encode_C_string(tc,
                            MVM_cu_string(tc, get_current_cu(tc, g, inline_stack),
                            ann->data.lineno.filename_string_index));
                        MVM_ds_appendf(ds, "      [Annotation: Line Number: %s:%d]\n",
                            cstr, ann->data.lineno.line_number);
                        MVM_free(cstr);
                    }
                    break;
                }
                case MVM_SPESH_ANN_LOGGED:
                    MVM_ds_appendf(ds, "      [Annotation: Logged (bytecode offset %d)]\n",
                        ann->data.bytecode_offset);
                    break;
                case MVM_SPESH_ANN_DEOPT_SYNTH:
                    MVM_ds_appendf(ds, "      [Annotation: INS Deopt Synth (idx %d)]\n",
                        ann->data.deopt_idx);
                    break;
                case MVM_SPESH_ANN_CACHED:
                    MVM_ds_appendf(ds, "      [Annotation: Cached (bytecode offset %d)]\n",
                        ann->data.bytecode_offset);
                    break;
                case MVM_SPESH_ANN_COMMENT:
                    num_comments++;
                    break;
                case MVM_SPESH_ANN_DELAYED_TEMPS:
                    MVM_ds_append(ds, "      [Delayed temps to release: ");
                    MVMSpeshOperand *ptr = ann->data.temps_to_release;
                    MVMuint16 insert_comma = 0;
                    while (ptr->lit_i64 != -1) {
                        if (insert_comma++) {
                            MVM_ds_append(ds, ", ");
                        }
                        MVM_ds_appendf(ds, "%d(%d)", ptr->reg.orig, ptr->reg.i);
                        ptr++;
                    }
                    MVM_ds_append(ds, ")]\n");
                    break;
                default:
                    MVM_ds_appendf(ds, "      [Annotation: %d (unknown)]\n", ann->type);
            }
            ann = ann->next;
        }
        while (pop_inlines--)
            pop_inline(tc, inline_stack);

        if (num_comments > 1) {
            ann = cur_ins->annotations;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_COMMENT) {
                    MVM_ds_appendf(ds, "      # [%03d] %s\n", ann->order, ann->data.comment);
                }
                ann = ann->next;
            }
        }

        MVM_ds_appendf(ds, "      %-15s ", cur_ins->info->name);
        if (cur_ins->info->opcode == MVM_SSA_PHI) {
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                MVMint16 orig = cur_ins->operands[i].reg.orig;
                MVMint16 regi = cur_ins->operands[i].reg.i;
                if (i)
                    MVM_ds_append(ds, ", ");
                if (orig < 10) MVM_ds_append(ds, " ");
                if (regi < 10) MVM_ds_append(ds, " ");
                MVM_ds_appendf(ds, "r%d(%d)", orig, regi);
            }
        }
        else {
            /* Count the opcode itself */
            size += 2;
            for (i = 0; i < cur_ins->info->num_operands; i++) {
                if (i)
                    MVM_ds_append(ds, ", ");
                switch (cur_ins->info->operands[i] & MVM_operand_rw_mask) {
                    case MVM_operand_read_reg:
                    case MVM_operand_write_reg: {
                        MVMint16 orig = cur_ins->operands[i].reg.orig;
                        MVMint16 regi = cur_ins->operands[i].reg.i;
                        if (orig < 10) MVM_ds_append(ds, " ");
                        if (regi < 10) MVM_ds_append(ds, " ");
                        MVM_ds_appendf(ds, "r%d(%d)", orig, regi);
                        size += 4;
                        break;
                    }
                    case MVM_operand_read_lex:
                    case MVM_operand_write_lex: {
                        MVMStaticFrameBody *cursor = &g->sf->body;
                        MVMuint32 ascension;
                        MVM_ds_appendf(ds, "lex(idx=%d,outers=%d", cur_ins->operands[i].lex.idx,
                            cur_ins->operands[i].lex.outers);
                        for (ascension = 0;
                                ascension < cur_ins->operands[i].lex.outers;
                                ascension++, cursor = &cursor->outer->body) { };
                        if (cursor->fully_deserialized) {
                            if (cur_ins->operands[i].lex.idx < cursor->num_lexicals) {
                                char *cstr = MVM_string_utf8_encode_C_string(tc, cursor->lexical_names_list[cur_ins->operands[i].lex.idx]);
                                MVM_ds_appendf(ds, ",%s)", cstr);
                                MVM_free(cstr);
                            } else {
                                MVM_ds_append(ds, ",<out of bounds>)");
                            }
                        } else {
                            MVM_ds_append(ds, ",<pending deserialization>)");
                        }
                        size += 4;
                        break;
                    }
                    case MVM_operand_literal: {
                        MVMuint32 type = cur_ins->info->operands[i] & MVM_operand_type_mask;
                        switch (type) {
                        case MVM_operand_ins: {
                            MVMint32 bb_idx = cur_ins->operands[i].ins_bb->idx;
                            if (bb_idx < 100) MVM_ds_append(ds, " ");
                            if (bb_idx < 10)  MVM_ds_append(ds, " ");
                            MVM_ds_appendf(ds, "BB(%d)", bb_idx);
                            size += 4;
                            break;
                        }
                        case MVM_operand_int8:
                            MVM_ds_appendf(ds, "liti8(%"PRId8")", cur_ins->operands[i].lit_i8);
                            size += 2;
                            break;
                        case MVM_operand_int16:
                            MVM_ds_appendf(ds, "liti16(%"PRId16")", cur_ins->operands[i].lit_i16);
                            size += 2;
                            break;
                        case MVM_operand_uint16:
                            MVM_ds_appendf(ds, "litui16(%"PRIu16")", cur_ins->operands[i].lit_ui16);
                            size += 2;
                            break;
                        case MVM_operand_int32:
                            MVM_ds_appendf(ds, "liti32(%"PRId32")", cur_ins->operands[i].lit_i32);
                            size += 4;
                            break;
                        case MVM_operand_uint32:
                            MVM_ds_appendf(ds, "litui32(%"PRIu32")", cur_ins->operands[i].lit_ui32);
                            size += 4;
                            break;
                        case MVM_operand_int64:
                            MVM_ds_appendf(ds, "liti64(%"PRId64")", cur_ins->operands[i].lit_i64);
                            size += 8;
                            break;
                        case MVM_operand_uint64:
                            MVM_ds_appendf(ds, "liti64(%"PRIu64")", cur_ins->operands[i].lit_ui64);
                            size += 8;
                            break;
                        case MVM_operand_num32:
                            MVM_ds_appendf(ds, "litn32(%f)", cur_ins->operands[i].lit_n32);
                            size += 4;
                            break;
                        case MVM_operand_num64:
                            MVM_ds_appendf(ds, "litn64(%g)", cur_ins->operands[i].lit_n64);
                            size += 8;
                            break;
                        case MVM_operand_str: {
                            char *cstr = MVM_string_utf8_encode_C_string(tc,
                                MVM_cu_string(tc, g->sf->body.cu, cur_ins->operands[i].lit_str_idx));
                            MVM_ds_appendf(ds, "lits(%s)", cstr);
                            MVM_free(cstr);
                            size += 8;
                            break;
                        }
                        case MVM_operand_callsite: {
                            MVMCallsite *callsite = g->sf->body.cu->body.callsites[cur_ins->operands[i].callsite_idx];
                            MVM_ds_appendf(ds, "callsite(%p, %d arg, %d pos, %s, %s)",
                                    callsite,
                                    callsite->flag_count, callsite->num_pos,
                                    callsite->has_flattening ? "flattening" : "nonflattening",
                                    callsite->is_interned ? "interned" : "noninterned");
                            size += 2;
                            break;

                        }
                        case MVM_operand_spesh_slot:
                            MVM_ds_appendf(ds, "sslot(%"PRId16")", cur_ins->operands[i].lit_i16);
                            size += 2;
                            break;
                        case MVM_operand_coderef: {
                            MVMCodeBody *body = &((MVMCode*)g->sf->body.cu->body.coderefs[cur_ins->operands[i].coderef_idx])->body;
                            MVMBytecodeAnnotation *anno = MVM_bytecode_resolve_annotation(tc, &body->sf->body, 0);

                            MVM_ds_append(ds, "coderef(");

                            if (anno) {
                                char *filestr = MVM_string_utf8_encode_C_string(tc,
                                    MVM_cu_string(tc, g->sf->body.cu, anno->filename_string_heap_index));
                                MVM_ds_appendf(ds, "%s:%d%s)", filestr, anno->line_number, body->outer ? " (closure)" : "");
                                MVM_free(filestr);
                            } else {
                                MVM_ds_append(ds, "??\?)");
                            }

                            size += 2;

                            MVM_free(anno);
                            break;
                        }
                        default:
                            MVM_ds_append(ds, "<nyi(lit)>");
                        }
                        break;
                    }
                    default:
                        MVM_ds_append(ds, "<nyi>");
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
                        MVM_ds_appendf(ds, " (%s: %s)", repr_name, debug_name);
                    } else {
                        MVM_ds_appendf(ds, " (%s: ?)", repr_name);
                    }
                } else {
                    MVM_ds_append(ds, " (not deserialized)");
                }
            }
        }
        if (num_comments == 1) {
            ann = cur_ins->annotations;
            while (ann) {
                if (ann->type == MVM_SPESH_ANN_COMMENT) {
                    MVM_ds_appendf(ds, "  # [%03d] %s", ann->order, ann->data.comment);
                    break;
                }
                ann = ann->next;
            }
        }
        MVM_ds_append(ds, "\n");
        cur_ins = cur_ins->next;
    }

    if (stats) {
        if (bb->inlined)
            stats->inlined_size += size;
        stats->total_size += size;
    }

    /* Predecessors and successors. */
    MVM_ds_append(ds, "    Successors: ");
    for (i = 0; i < bb->num_succ; i++)
        MVM_ds_appendf(ds, (i == 0 ? "%d" : ", %d"), bb->succ[i]->idx);
    MVM_ds_append(ds, "\n    Predecessors: ");
    for (i = 0; i < bb->num_pred; i++)
        MVM_ds_appendf(ds, (i == 0 ? "%d" : ", %d"), bb->pred[i]->idx);
    MVM_ds_append(ds, "\n    Dominance children: ");
    for (i = 0; i < bb->num_children; i++)
        MVM_ds_appendf(ds, (i == 0 ? "%d" : ", %d"), bb->children[i]->idx);
    MVM_ds_append(ds, "\n\n");
}

/* Dump deopt usages. */
static void dump_deopt_usages(MVMThreadContext *tc, MVMDumpStr *ds, MVMSpeshGraph *g, MVMSpeshOperand operand) {
    MVMSpeshFacts *facts = MVM_spesh_get_facts(tc, g, operand);
    MVMSpeshDeoptUseEntry *entry = facts->usage.deopt_users;
    if (entry) {
        MVMuint32 first = 1;
        MVM_ds_append(ds, ", deopt=");
        while (entry) {
            if (first)
                first = 0;
            else
                MVM_ds_append(ds, ",");
            MVM_ds_appendf(ds, "%d", entry->deopt_idx);
            entry = entry->next;
        }
    }
}
/* Dumps the facts table. */
static void dump_facts(MVMThreadContext *tc, MVMDumpStr *ds, MVMSpeshGraph *g) {
    MVMuint16 i, j, num_locals, num_facts;
    num_locals = g->num_locals;
    for (i = 0; i < num_locals; i++) {
        num_facts = g->fact_counts[i];
        for (j = 0; j < num_facts; j++) {
            MVMint32 flags = g->facts[i][j].flags;
            MVMSpeshOperand operand;
            operand.reg.orig = i;
            operand.reg.i = j;
            if (i < 10) MVM_ds_append(ds, " ");
            if (j < 10) MVM_ds_append(ds, " ");
            if (flags || g->facts[i][j].dead_writer || (g->facts[i][j].writer && g->facts[i][j].writer->info->opcode == MVM_SSA_PHI)) {
                MVM_ds_appendf(ds, "    r%d(%d): usages=%d%s", i, j,
                    MVM_spesh_usages_count(tc, g, operand),
                    MVM_spesh_usages_is_used_by_handler(tc, g, operand) ? "+handler" : "");
                dump_deopt_usages(tc, ds, g, operand);
                MVM_ds_appendf(ds, ", flags=%-5d", flags);
                if (flags & 1) {
                    MVM_ds_append(ds, " KnTyp");
                }
                if (flags & 2) {
                    MVM_ds_append(ds, " KnVal");
                }
                if (flags & 4) {
                    MVM_ds_append(ds, " Dcntd");
                }
                if (flags & 8) {
                    MVM_ds_append(ds, " Concr");
                }
                if (flags & 16) {
                    MVM_ds_append(ds, " TyObj");
                }
                if (flags & 32) {
                    MVM_ds_append(ds, " KnDcT");
                }
                if (flags & 64) {
                    MVM_ds_append(ds, " DCncr");
                }
                if (flags & 128) {
                    MVM_ds_append(ds, " DcTyO");
                }
                if (flags & 256) {
                    MVM_ds_append(ds, " LogGd");
                }
                if (flags & 512) {
                    MVM_ds_append(ds, " HashI");
                }
                if (flags & 1024) {
                    MVM_ds_append(ds, " ArrIt");
                }
                if (flags & 2048) {
                    MVM_ds_append(ds, " KBxSr");
                }
                if (flags & 4096) {
                    MVM_ds_append(ds, " MgWLG");
                }
                if (flags & 8192) {
                    MVM_ds_append(ds, " KRWCn");
                }
                if (g->facts[i][j].dead_writer) {
                    MVM_ds_append(ds, " DeadWriter");
                }
                if (g->facts[i][j].writer && g->facts[i][j].writer->info->opcode == MVM_SSA_PHI) {
                    MVM_ds_appendf(ds, " (merged from %d regs)", g->facts[i][j].writer->info->num_operands - 1);
                }
                if (flags & 1) {
                    MVM_ds_appendf(ds, " (type: %s)", MVM_6model_get_debug_name(tc, g->facts[i][j].type));
                }
            }
            else {
                MVM_ds_appendf(ds, "    r%d(%d): usages=%d%s", i, j,
                    MVM_spesh_usages_count(tc, g, operand),
                    MVM_spesh_usages_is_used_by_handler(tc, g, operand) ? "+handler" : "");
                dump_deopt_usages(tc, ds, g, operand);
                MVM_ds_appendf(ds, ", flags=%-5d", flags);
            }
            MVM_ds_append(ds, "\n");
        }
        MVM_ds_append(ds, "\n");
    }
}

static void dump_callsite(MVMThreadContext *tc, MVMDumpStr *ds, MVMCallsite *cs, char *indent) {
    MVMuint16 i;
    MVM_ds_appendf(ds, "Callsite %p (%d args, %d pos)\n", cs, cs->flag_count, cs->num_pos);
    for (i = 0; i < cs->flag_count - cs->num_pos; i++) {
        char * argname_utf8 = MVM_string_utf8_encode_C_string(tc, cs->arg_names[i]);
        MVM_ds_appendf(ds, "%s  - %s\n", indent, argname_utf8);
        MVM_free(argname_utf8);
    }
    if (cs->num_pos)
        MVM_ds_appendf(ds, "%sPositional flags: ", indent);
    for (i = 0; i < cs->num_pos; i++) {
        MVMCallsiteEntry arg_flag = cs->arg_flags[i];
        MVMCallsiteEntry arg_type = arg_flag & MVM_CALLSITE_ARG_TYPE_MASK;
        MVMCallsiteEntry other_flags = arg_flag & ~MVM_CALLSITE_ARG_TYPE_MASK;

        if (i)
            MVM_ds_append(ds, ", ");

        if (arg_type == MVM_CALLSITE_ARG_OBJ) {
            MVM_ds_append(ds, "obj");
        } else if (arg_type == MVM_CALLSITE_ARG_INT) {
            MVM_ds_append(ds, "int");
        } else if (arg_type == MVM_CALLSITE_ARG_UINT) {
            MVM_ds_append(ds, "uint");
        } else if (arg_type == MVM_CALLSITE_ARG_NUM) {
            MVM_ds_append(ds, "num");
        } else if (arg_type == MVM_CALLSITE_ARG_STR) {
            MVM_ds_append(ds, "str");
        }
        if (other_flags) {
            if (other_flags == MVM_CALLSITE_ARG_LITERAL) {
                MVM_ds_append(ds, "lit");
            }
            else {
                MVM_ds_appendf(ds, "??%d", arg_flag);
            }
        }
    }
    if (cs->num_pos)
        MVM_ds_append(ds, "\n");
    MVM_ds_append(ds, "\n");
}

static void dump_fileinfo(MVMThreadContext *tc, MVMDumpStr *ds, MVMStaticFrame *sf) {
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
    MVM_ds_appendf(ds, "%s:%d", filename_utf8, line_nr);
    if (filename)
        MVM_free(filename_utf8);
    MVM_free(ann);
}

static void dump_deopt_pea(MVMThreadContext *tc, MVMDumpStr *ds, MVMSpeshGraph *g) {
    MVMuint32 i, j;
    if (MVM_VECTOR_ELEMS(g->deopt_pea.materialize_info)) {
        MVM_ds_append(ds, "\nMaterializations:\n");
        for (i = 0; i < MVM_VECTOR_ELEMS(g->deopt_pea.materialize_info); i++) {
            MVMSpeshPEAMaterializeInfo *mat = &(g->deopt_pea.materialize_info[i]);
            MVMSTable *st = (MVMSTable *)g->spesh_slots[mat->stable_sslot];
            MVM_ds_appendf(ds, "  %d: %s from regs ", i, st->debug_name);
            for (j = 0; j < mat->num_attr_regs; j++)
                MVM_ds_appendf(ds, j > 0 ? ", r%hu" : "r%hu", mat->attr_regs[j]);
            MVM_ds_append(ds, "\n");
        }
    }
    if (MVM_VECTOR_ELEMS(g->deopt_pea.deopt_point)) {
        MVM_ds_append(ds, "\nDeopt point materialization mappings:\n");
        for (i = 0; i < MVM_VECTOR_ELEMS(g->deopt_pea.deopt_point); i++) {
            MVMSpeshPEADeoptPoint *dp = &(g->deopt_pea.deopt_point[i]);
            MVM_ds_appendf(ds, "  At %d materialize %d into r%d\n", dp->deopt_point_idx,
                    dp->materialize_info_idx, dp->target_reg);
        }
    }
}

void dump_spesh_slots(MVMThreadContext *tc, MVMDumpStr *ds,
                      MVMuint32 num_spesh_slots, MVMCollectable **spesh_slots,
                      MVMuint8 show_pointers) {
    MVMuint32 i;
    MVM_ds_append(ds, "\nSpesh slots:\n");
    for (i = 0; i < num_spesh_slots; i++) {
        MVMCollectable *value = spesh_slots[i];
        if (value == NULL)
            MVM_ds_appendf(ds, "    %d = NULL\n", i);
        else if (value->flags1 & MVM_CF_STABLE)
            MVM_ds_appendf(ds, "    %d = STable (%s)\n", i,
                MVM_6model_get_stable_debug_name(tc, (MVMSTable *)value));
        else if (value->flags1 & MVM_CF_TYPE_OBJECT)
            MVM_ds_appendf(ds, "    %d = Type Object (%s)\n", i,
                MVM_6model_get_debug_name(tc, (MVMObject *)value));
        else {
            MVMObject *obj = (MVMObject *)value;
            MVMuint32 repr_id = REPR(obj)->ID;
            MVM_ds_appendf(ds, "    %d = Instance (%s)", i,
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
                MVM_ds_appendf(ds, " - '%s' (%s)", name_str, cuuid_str);
                MVM_free(name_str);
                MVM_free(cuuid_str);
            }
            MVM_ds_appendf(ds, "\n");
        }
    }
}



/* Dump a spesh graph into string form, for debugging purposes. */
char * MVM_spesh_dump(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Allocate buffer. */
    MVMDumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    MVM_spesh_dump_to_ds(tc, g, &ds);

    return ds.buffer;
}
void MVM_spesh_dump_to_ds(MVMThreadContext *tc, MVMSpeshGraph *g, MVMDumpStr *ds) {
    MVMSpeshBB *cur_bb;
    SpeshGraphSizeStats stats;
    InlineIndexStack inline_stack;

    stats.total_size = 0;
    stats.inlined_size = 0;
    inline_stack.cur_depth = -1;

    /* Dump name and CUID. */
    MVM_ds_append(ds, "Spesh of '");
    MVM_ds_append_str(tc, ds, g->sf->body.name);
    MVM_ds_append(ds, "' (cuid: ");
    MVM_ds_append_str(tc, ds, g->sf->body.cuuid);
    MVM_ds_append(ds, ", file: ");
    dump_fileinfo(tc, ds, g->sf);
    MVM_ds_append(ds, ")\n");
    if (g->cs)
        dump_callsite(tc, ds, g->cs, "");
    if (!g->cs)
        MVM_ds_append(ds, "\n");

    /* Go over all the basic blocks and dump them. */
    cur_bb = g->entry;
    while (cur_bb) {
        dump_bb(tc, ds, g, cur_bb, &stats, &inline_stack);
        cur_bb = cur_bb->linear_next;
    }

    /* Dump facts. */
    if (g->facts) {
        MVM_ds_append(ds, "\nFacts:\n");
        dump_facts(tc, ds, g);
    }

    /* Dump spesh slots. */
    if (g->num_spesh_slots) {
        dump_spesh_slots(tc, ds, g->num_spesh_slots, g->spesh_slots, 0);
    }

    /* Dump materialization deopt into. */
    dump_deopt_pea(tc, ds, g);

    MVM_ds_append(ds, "\n");

    /* Print out frame size */
    if (stats.inlined_size)
        MVM_ds_appendf(ds, "Frame size: %u bytes (%u from inlined frames)\n", stats.total_size, stats.inlined_size);
    else
        MVM_ds_appendf(ds, "Frame size: %u bytes\n", stats.total_size);
}

/* Dumps a spesh stats type typle. */
static void dump_stats_type_tuple(MVMThreadContext *tc, MVMDumpStr *ds, MVMCallsite *cs,
                                  MVMSpeshStatsType *type_tuple, char *prefix) {
    MVMuint32 j;
    for (j = 0; j < cs->flag_count; j++) {
        MVMObject *type = type_tuple[j].type;
        if (type) {
            MVMObject *decont_type = type_tuple[j].decont_type;
            MVM_ds_appendf(ds, "%sType %d: %s%s (%s)",
                prefix, j,
                (type_tuple[j].rw_cont ? "RW " : ""),
                MVM_6model_get_stable_debug_name(tc, type->st),
                (type_tuple[j].type_concrete ? "Conc" : "TypeObj"));
            if (decont_type)
                MVM_ds_appendf(ds, " of %s (%s)",
                    MVM_6model_get_stable_debug_name(tc, decont_type->st),
                    (type_tuple[j].decont_type_concrete ? "Conc" : "TypeObj"));
            MVM_ds_append(ds, "\n");
        }
    }
}

/* Dumps the statistics associated with a particular callsite object. */
static void dump_stats_by_callsite(MVMThreadContext *tc, MVMDumpStr *ds, MVMSpeshStatsByCallsite *css) {
    MVMuint32 i, j, k;

    if (css->cs)
        dump_callsite(tc, ds, css->cs, "");
    else
        MVM_ds_append(ds, "No interned callsite\n");
    MVM_ds_appendf(ds, "    Callsite hits: %d\n\n", css->hits);
    if (css->osr_hits)
        MVM_ds_appendf(ds, "    OSR hits: %d\n\n", css->osr_hits);
    MVM_ds_appendf(ds, "    Maximum stack depth: %d\n\n", css->max_depth);

    for (i = 0; i < css->num_by_type; i++) {
        MVMSpeshStatsByType *tss = &(css->by_type[i]);
        MVM_ds_appendf(ds, "    Type tuple %d\n", i);
        dump_stats_type_tuple(tc, ds, css->cs, tss->arg_types, "        ");
        MVM_ds_appendf(ds, "        Hits: %d\n", tss->hits);
        if (tss->osr_hits)
            MVM_ds_appendf(ds, "        OSR hits: %d\n", tss->osr_hits);
        MVM_ds_appendf(ds, "        Maximum stack depth: %d\n", tss->max_depth);
        if (tss->num_by_offset) {
            MVM_ds_append(ds, "        Logged at offset:\n");
            for (j = 0; j < tss->num_by_offset; j++) {
                MVMSpeshStatsByOffset *oss = &(tss->by_offset[j]);
                MVM_ds_appendf(ds, "            %d:\n", oss->bytecode_offset);
                for (k = 0; k < oss->num_types; k++)
                    MVM_ds_appendf(ds, "                %d x type %s (%s)\n",
                        oss->types[k].count,
                        MVM_6model_get_stable_debug_name(tc, oss->types[k].type->st),
                        (oss->types[k].type_concrete ? "Conc" : "TypeObj"));
                for (k = 0; k < oss->num_invokes; k++) {
                    char *body_name = MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.name);
                    char *body_cuuid = MVM_string_utf8_encode_C_string(tc, oss->invokes[k].sf->body.cuuid);
                    MVM_ds_appendf(ds,
                        "                %d x static frame '%s' (%s) (caller is outer: %d)\n",
                        oss->invokes[k].count,
                        body_name,
                        body_cuuid,
                        oss->invokes[k].caller_is_outer_count);
                    MVM_free(body_name);
                    MVM_free(body_cuuid);
                }
                for (k = 0; k < oss->num_type_tuples; k++) {
                    MVM_ds_appendf(ds, "                %d x type tuple:\n",
                        oss->type_tuples[k].count);
                    dump_stats_type_tuple(tc, ds, oss->type_tuples[k].cs,
                        oss->type_tuples[k].arg_types,
                        "                    ");
                }
                for (k = 0; k < oss->num_dispatch_results; k++)
                    MVM_ds_appendf(ds, "                %d x dispatch result index %d\n",
                        oss->dispatch_results[k].count,
                        oss->dispatch_results[k].result_index);
            }
        }
        MVM_ds_append(ds, "\n");
    }
}

/* Dumps the statistics associated with a static frame into a string. */
void MVM_spesh_dump_stats(MVMThreadContext *tc, MVMStaticFrame *sf, MVMDumpStr *ds) {
    MVMSpeshStats *ss = sf->body.spesh->body.spesh_stats;

    /* Dump name and CUID. */
    MVM_ds_append(ds, "Latest statistics for '");
    MVM_ds_append_str(tc, ds, sf->body.name);
    MVM_ds_append(ds, "' (cuid: ");
    MVM_ds_append_str(tc, ds, sf->body.cuuid);
    MVM_ds_append(ds, ", file: ");
    dump_fileinfo(tc, ds, sf);
    MVM_ds_append(ds, ")\n\n");

    /* Dump the spesh stats if present. */
    if (ss) {
        MVMuint32 i;

        MVM_ds_appendf(ds, "Total hits: %d\n", ss->hits);
        if (ss->osr_hits)
            MVM_ds_appendf(ds, "OSR hits: %d\n", ss->osr_hits);
        MVM_ds_append(ds, "\n");

        for (i = 0; i < ss->num_by_callsite; i++)
            dump_stats_by_callsite(tc, ds, &(ss->by_callsite[i]));
    }
    else {
        MVM_ds_append(ds, "No spesh stats for this static frame\n");
    }

    MVM_ds_append(ds, "\n");
}

/* Dumps a planned specialization into a string. */
void MVM_spesh_dump_planned(MVMThreadContext *tc, MVMSpeshPlanned *p, MVMDumpStr *ds) {
    /* Dump kind of specialization and target. */
    switch (p->kind) {
        case MVM_SPESH_PLANNED_CERTAIN:
            MVM_ds_append(ds, "Certain");
            break;
        case MVM_SPESH_PLANNED_OBSERVED_TYPES:
            MVM_ds_append(ds, "Observed type");
            break;
        case MVM_SPESH_PLANNED_DERIVED_TYPES:
            MVM_ds_append(ds, "Derived type");
            break;
    }
    MVM_ds_append(ds, " specialization of '");
    MVM_ds_append_str(tc, ds, p->sf->body.name);
    MVM_ds_append(ds, "' (cuid: ");
    MVM_ds_append_str(tc, ds, p->sf->body.cuuid);
    MVM_ds_append(ds, ", file: ");
    dump_fileinfo(tc, ds, p->sf);
    MVM_ds_append(ds, ")\n\n");

    /* Dump the callsite of the specialization. */
    if (p->cs_stats->cs) {
        MVM_ds_append(ds, "The specialization is for the callsite:\n");
        dump_callsite(tc, ds, p->cs_stats->cs, "");
    }
    else {
        MVM_ds_append(ds, "The specialization is for when there is no interned callsite.\n");
    }

    /* Dump reasoning. */
    switch (p->kind) {
        case MVM_SPESH_PLANNED_CERTAIN:
            if (p->cs_stats->hits >= MVM_spesh_threshold(tc, p->sf))
                MVM_ds_appendf(ds,
                    "It was planned due to the callsite receiving %u hits.\n",
                    p->cs_stats->hits);
            else if (p->cs_stats->osr_hits >= MVM_SPESH_PLAN_CS_MIN_OSR)
                MVM_ds_appendf(ds,
                    "It was planned due to the callsite receiving %u OSR hits.\n",
                    p->cs_stats->osr_hits);
            else
                MVM_ds_append(ds, "It was planned for unknown reasons.\n");
            if (!p->sf->body.specializable)
                MVM_ds_append(ds, "The body contains no specializable instructions.\n");
            break;
        case MVM_SPESH_PLANNED_OBSERVED_TYPES: {
            MVMCallsite *cs = p->cs_stats->cs;
            MVMuint32 hit_percent = p->cs_stats->hits
               ? (100 * p->type_stats[0]->hits) / p->cs_stats->hits
               : 0;
            MVMuint32 osr_hit_percent = p->cs_stats->osr_hits
               ? (100 * p->type_stats[0]->osr_hits) / p->cs_stats->osr_hits
               : 0;
            MVM_ds_append(ds, "It was planned for the type tuple:\n");
            dump_stats_type_tuple(tc, ds, cs, p->type_tuple, "    ");
            if (osr_hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT_OSR)
                MVM_ds_appendf(ds, "Which received %u OSR hits (%u%% of the %u callsite OSR hits).\n",
                    p->type_stats[0]->osr_hits, osr_hit_percent, p->cs_stats->osr_hits);
            else if (hit_percent >= MVM_SPESH_PLAN_TT_OBS_PERCENT)
                MVM_ds_appendf(ds, "Which received %u hits (%u%% of the %u callsite hits).\n",
                    p->type_stats[0]->hits, hit_percent, p->cs_stats->hits);
            else
                MVM_ds_append(ds, "For unknown reasons.\n");
            break;
        }
        case MVM_SPESH_PLANNED_DERIVED_TYPES: {
            MVMCallsite *cs = p->cs_stats->cs;
            MVM_ds_append(ds, "It was planned for the type tuple:\n");
            dump_stats_type_tuple(tc, ds, cs, p->type_tuple, "    ");
            break;
        }
    }

    MVM_ds_appendf(ds, "\nThe maximum stack depth is %d.\n\n", p->max_depth);
}

static const char *register_type(MVMint8 reg_type) {
    switch (reg_type) {
    case MVM_reg_int8: return "int8";
    case MVM_reg_int16: return "int16";
    case MVM_reg_int32: return "int32";
    case MVM_reg_int64: return "int64";
    case MVM_reg_num32: return "num32";
    case MVM_reg_num64: return "num64";
    case MVM_reg_str: return "str";
    case MVM_reg_obj: return "obj";
    case MVM_reg_uint8: return "uint8";
    case MVM_reg_uint16: return "uint16";
    case MVM_reg_uint32: return "uint32";
    case MVM_reg_uint64: return "uint64";
    default: return "unknown";
    }
}

#ifdef DEBUG_HELPERS
char * MVM_spesh_dump_register_layout(MVMThreadContext *tc, MVMFrame *f) {
    MVMDumpStr ds;
    ds.alloc  = 8192;
    ds.buffer = MVM_malloc(ds.alloc);
    ds.pos    = 0;

    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMuint16 num_locals = cand ? cand->body.num_locals : f->static_info->body.work_size / sizeof(MVMRegister);
    MVMuint16 *local_types = cand ? cand->body.local_types : f->static_info->body.local_types;

    MVMuint16 num_inlines = cand ? cand->body.num_inlines : 0;

    if (num_inlines) {
        MVM_ds_append(&ds, "Inlines:\n");
    }
    for (MVMuint16 inl_idx = 0; inl_idx < num_inlines; inl_idx++) {
        MVMSpeshInline *inl = &cand->body.inlines[inl_idx];

        MVM_ds_appendf(&ds, "  - %2u '", inl_idx);
        MVM_ds_append_str(tc, &ds, inl->sf->body.name);
        MVM_ds_append(&ds, "' (cuuid ");
        MVM_ds_append_str(tc, &ds, inl->sf->body.cuuid);
        MVM_ds_append(&ds, ")\n    ");
        MVM_ds_appendf(&ds, "    bytecode from % 4d to % 4d\n    ", inl->start, inl->end);
        dump_callsite(tc, &ds, inl->cs, "    ");
    }

    if (f->params.arg_info.callsite->flag_count > 0) {
        MVM_ds_append(&ds, "Parameters:\n");
        MVM_ds_appendf(&ds, "  source: f->");
        MVMFrame *fp = f;

        while (fp != NULL) {
            if (fp->work == f->params.arg_info.source) {
                MVM_ds_appendf(&ds, "work: (MVMRegister *)%p\n", f->params.arg_info.source);
                MVM_ds_appendf(&ds, "  frame of source: (MVMFrame *)%p\n", fp);
                break;
            }
            fp = fp->caller;
            MVM_ds_append(&ds, "caller->");
        }
        if (fp == NULL) {
            MVM_ds_append(&ds, "...??? not found - flattening involved?\n");
        }

        MVM_ds_append(&ds, "  callsite of params: ");
        dump_callsite(tc, &ds, f->params.arg_info.callsite, "    ");
        MVM_ds_append(&ds, "\n");
    }

    MVM_ds_append(&ds, "Locals (registers)\n");

    for (MVMuint16 loc_idx = 0; loc_idx < num_locals; loc_idx++) {
        MVMuint16 type = local_types[loc_idx];

        MVM_ds_appendf(&ds, "  %3u: (%7s) ", loc_idx, register_type(type));

        if (type == MVM_reg_obj) {
            MVMObject *ov = f->work[loc_idx].o;
            if (ov)
                MVM_ds_appendf(&ds, "%p (%s of %s name %s) ", ov, IS_CONCRETE(ov) ? "conc" : "type", REPR(ov)->name, MVM_6model_get_debug_name(tc, ov));
            else
                MVM_ds_appendf(&ds, "%p ", ov);
        }
        else if (type == MVM_reg_str) {
            MVM_ds_appendf(&ds, "%p ", f->work[loc_idx].s);
        }
        else if (type == MVM_reg_int64) {
            MVM_ds_appendf(&ds, "%s0x%"PRIx64" ", f->work[loc_idx].i64 < 0 ? "-" : "", f->work[loc_idx].u64);
        }
        else if (type == MVM_reg_uint64) {
            MVM_ds_appendf(&ds, "0x%"PRIx64" ", f->work[loc_idx].u64);
        }

        for (MVMuint16 inl_idx = 0; inl_idx < num_inlines; inl_idx++) {
            MVMSpeshInline *inl = &cand->body.inlines[inl_idx];

            MVMuint8 is_start = inl->locals_start == loc_idx ? 1 : 0;
            MVMuint8 is_coderef = inl->code_ref_reg == loc_idx ? 1 : 0;
            MVMuint8 is_res = inl->res_reg == loc_idx ? 1 : 0;
            MVMuint8 is_end = inl->locals_start + inl->num_locals - 1 == loc_idx ? 1 : 0;

            if (is_start) { MVM_ds_appendf(&ds, " [start of inline %d's registers]", inl_idx); }
            if (is_coderef) { MVM_ds_appendf(&ds, " [inline %d's code ref register]", inl_idx); }
            if (is_res) { MVM_ds_appendf(&ds, " [inline %d's result register]", inl_idx); }
            if (is_end) { MVM_ds_appendf(&ds, " [last of %d's registers]", inl_idx); }
        }

        MVM_ds_append(&ds, "\n");
    }

    if (cand) {
        dump_spesh_slots(tc, &ds, cand->body.num_spesh_slots, f->effective_spesh_slots, 1);
    }

    MVM_ds_append_null(&ds);
    return ds.buffer;
}
#endif

/* Dumps a static frame's guard set into a string. */
void MVM_spesh_dump_arg_guard(MVMThreadContext *tc, MVMStaticFrame *sf, MVMSpeshArgGuard *ag, MVMDumpStr *ds) {
    /* Dump name and CUID. */
    if (sf) {
        MVM_ds_append(ds, "Latest guard tree for '");
        MVM_ds_append_str(tc, ds, sf->body.name);
        MVM_ds_append(ds, "' (cuid: ");
        MVM_ds_append_str(tc, ds, sf->body.cuuid);
        MVM_ds_append(ds, ", file: ");
        dump_fileinfo(tc, ds, sf);
        MVM_ds_append(ds, ")\n\n");
    }

    /* Dump nodes. */
    if (ag) {
        MVMuint32 i = 0;
        for (i = 0; i < ag->used_nodes; i++) {
            MVMSpeshArgGuardNode *agn = &(ag->nodes[i]);
            switch (agn->op) {
                case MVM_SPESH_GUARD_OP_CALLSITE:
                    MVM_ds_appendf(ds, "%u: CALLSITE %p | Y: %u, N: %u\n",
                        i, agn->cs, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_LOAD_ARG:
                    MVM_ds_appendf(ds, "%u: LOAD ARG %d | Y: %u\n",
                        i, agn->arg_index, agn->yes);
                    break;
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                    MVM_ds_appendf(ds, "%u: STABLE CONC %s | Y: %u, N: %u\n",
                        i, MVM_6model_get_stable_debug_name(tc, agn->st), agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    MVM_ds_appendf(ds, "%u: STABLE CONC %s | Y: %u, N: %u\n",
                        i, MVM_6model_get_stable_debug_name(tc, agn->st), agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_VALUE:
                    MVM_ds_appendf(ds, "%u: DEREF_VALUE %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_DEREF_RW:
                    MVM_ds_appendf(ds, "%u: DEREF_RW %u | Y: %u, N: %u\n",
                        i, agn->offset, agn->yes, agn->no);
                    break;
                case MVM_SPESH_GUARD_OP_RESULT:
                    MVM_ds_appendf(ds, "%u: RESULT %u\n", i, agn->result);
                    break;
            }
        }
    }
    else {
        MVM_ds_append(ds, "No argument guard nodes\n");
    }

    MVM_ds_append(ds, "\n");
}
