#include "moar.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

static void instrument_graph_with_breakpoints(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry->linear_next;

    MVMint32 last_filename = -1;
    MVMint64 last_line_number = -1;

    char *filename_buf = NULL;

    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        MVMSpeshIns *breakpoint_ins;

        MVMBytecodeAnnotation *bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
        MVMint64 line_number = -1;
        MVMint64 filename_string_index = -1;

        MVMuint32 file_bp_idx;

        if (bbba) {
            line_number = bbba->line_number;
            filename_string_index = bbba->filename_string_heap_index;
            MVM_free(bbba);
        } else {
            bb = bb->linear_next;
            continue;
        }

        /* skip PHI instructions, to make sure PHI only occur uninterrupted after start-of-bb */
        while (ins && ins->info->opcode == MVM_SSA_PHI) {
            ins = ins->next;
        }
        if (!ins) ins = bb->last_ins;

        /* Jumplists require the target BB to start in the goto op.
         * We must not break this, or we cause the interpreter to derail */
        if (bb->last_ins->info->opcode == MVM_OP_jumplist) {
            MVMint16 to_skip = bb->num_succ;
            for (; to_skip > 0; to_skip--) {
                bb = bb->linear_next;
            }
            continue;
        }

        if (line_number != last_line_number) {
            breakpoint_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
            breakpoint_ins->info        = MVM_op_get_op(MVM_OP_breakpoint);
            breakpoint_ins->operands    = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));

            if (last_filename != filename_string_index) {
                if (filename_buf)
                    MVM_free(filename_buf);
                filename_buf = MVM_string_utf8_encode_C_string(tc, MVM_cu_string(tc, g->sf->body.cu, filename_string_index));
            }

            MVM_debugserver_register_line(tc, filename_buf, strlen(filename_buf), line_number, &file_bp_idx);

            breakpoint_ins->operands[0].lit_i32 = file_bp_idx;
            breakpoint_ins->operands[1].lit_i32 = line_number;

            last_filename = filename_string_index;
            last_line_number = line_number;

            MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, breakpoint_ins);
        }

        /* Now go through instructions to see if any are annotated with a
         * precise filename/lineno as well. */
        while (ins) {
            MVMSpeshAnn *ann = ins->annotations;

            while (ann) {
                if (ann->type == MVM_SPESH_ANN_LINENO) {
                    /* We are very likely to have one instruction here that has
                     * the same annotation as the bb itself. We skip that one.*/
                    if (ann->data.lineno.line_number == line_number && ann->data.lineno.filename_string_index == filename_string_index) {
                        break;
                    }

                    line_number = ann->data.lineno.line_number;
                    filename_string_index = ann->data.lineno.filename_string_index;

                    /*breakpoint_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));*/
                    /*breakpoint_ins->info        = MVM_op_get_op(MVM_OP_breakpoint);*/
                    /*breakpoint_ins->operands    = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));*/

                    if (last_filename != filename_string_index) {
                        if (filename_buf)
                            MVM_free(filename_buf);
                        filename_buf = MVM_string_utf8_encode_C_string(tc, MVM_cu_string(tc, g->sf->body.cu, filename_string_index));
                    }

                    MVM_debugserver_register_line(tc, filename_buf, strlen(filename_buf), line_number, &file_bp_idx);

                    /*breakpoint_ins->operands[0].lit_i32 = file_bp_idx;*/
                    /*breakpoint_ins->operands[1].lit_i32 = ann->data.lineno.line_number;*/

                    /* XXX insert breakpoint op here, too, maybe? */

                    break;
                }

                ann = ann->next;
            }

            ins = ins->next;
        }

        bb = bb->linear_next;
    }

    if (filename_buf)
        MVM_free(filename_buf);
}

static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry->linear_next;
    MVMuint16 array_slot = 0;

    MVMint32 last_line_number = -2;
    MVMint32 last_filename = -1;

    MVMuint16 allocd_slots  = g->num_bbs * 2;
    char *line_report_store = MVM_calloc(allocd_slots, sizeof(char));

    /* Since we don't know the right size for the line report store
     * up front, we will have to realloc it along the way. After that
     * we havee to fix up the arguments to the coverage log instructions */
    MVMuint32 fixup_alloc = g->num_bbs * 2;
    MVMuint32 fixup_elems = 0;
    MVMuint32 fixup_idx; /* for iterating over the fixup array */
    MVMSpeshIns **to_fixup = MVM_malloc(fixup_alloc * sizeof(MVMSpeshIns*));

    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        MVMSpeshIns *log_ins;

        MVMBytecodeAnnotation *bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
        MVMint64 line_number;
        MVMint64 filename_string_index;
        if (bbba) {
            line_number = bbba->line_number;
            filename_string_index = bbba->filename_string_heap_index;
            MVM_free(bbba);
        } else {
            bb = bb->linear_next;
            continue;
        }

        /* skip PHI instructions, to make sure PHI only occur uninterrupted after start-of-bb */
        while (ins && ins->info->opcode == MVM_SSA_PHI) {
            ins = ins->next;
        }
        if (!ins) ins = bb->last_ins;

        /* Jumplists require the target BB to start in the goto op.
         * We must not break this, or we cause the interpreter to derail */
        if (bb->last_ins->info->opcode == MVM_OP_jumplist) {
            MVMint16 to_skip = bb->num_succ;
            for (; to_skip > 0; to_skip--) {
                bb = bb->linear_next;
            }
            continue;
        }

        log_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
        log_ins->info        = MVM_op_get_op(MVM_OP_coverage_log);
        log_ins->operands    = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));

        log_ins->operands[0].lit_str_idx = filename_string_index;
        log_ins->operands[1].lit_i32 = line_number;

        if (last_line_number == line_number && last_filename == filename_string_index) {
            /* Consecutive BBs with the same line number and filename should
             * share one "already reported" slot. */
            log_ins->operands[2].lit_i32 = array_slot;
        } else {
            log_ins->operands[2].lit_i32 = array_slot++;
            last_line_number = line_number;
            last_filename = filename_string_index;

            if (array_slot == allocd_slots) {
                allocd_slots *= 2;
                line_report_store = MVM_realloc(line_report_store, sizeof(char) * allocd_slots);
            }
        }

        to_fixup[fixup_elems++] = log_ins;
        if (fixup_elems == fixup_alloc) {
            fixup_alloc *= 2;
            to_fixup = MVM_realloc(to_fixup, sizeof(MVMSpeshIns*) * fixup_alloc);
        }
        MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);

        /* Now go through instructions to see if any are annotated with a
         * precise filename/lineno as well. */
        while (ins) {
            MVMSpeshAnn *ann = ins->annotations;

            while (ann) {
                if (ann->type == MVM_SPESH_ANN_LINENO) {
                    /* We are very likely to have one instruction here that has
                     * the same annotation as the bb itself. We skip that one.*/
                    if (ann->data.lineno.line_number == line_number && ann->data.lineno.filename_string_index == filename_string_index) {
                        break;
                    }

                    log_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                    log_ins->info        = MVM_op_get_op(MVM_OP_coverage_log);
                    log_ins->operands    = MVM_spesh_alloc(tc, g, 4 * sizeof(MVMSpeshOperand));

                    log_ins->operands[0].lit_str_idx = ann->data.lineno.filename_string_index;
                    log_ins->operands[1].lit_i32 = ann->data.lineno.line_number;
                    log_ins->operands[2].lit_i32 = array_slot++;

                    if (array_slot == allocd_slots) {
                        allocd_slots *= 2;
                        line_report_store = MVM_realloc(line_report_store, sizeof(char) * allocd_slots);
                    }

                    to_fixup[fixup_elems++] = log_ins;
                    if (fixup_elems == fixup_alloc) {
                        fixup_alloc *= 2;
                        to_fixup = MVM_realloc(to_fixup, sizeof(MVMSpeshIns*) * fixup_alloc);
                    }
                    break;
                }

                ann = ann->next;
            }

            ins = ins->next;
        }

        bb = bb->linear_next;
    }

    line_report_store = MVM_realloc(line_report_store, sizeof(char) * (array_slot + 1));

    for (fixup_idx = 0; fixup_idx < fixup_elems; fixup_idx++) {
        MVMSpeshIns *ins = to_fixup[fixup_idx];

        ins->operands[3].lit_i64 = (uintptr_t)line_report_store;
    }

    if (array_slot == 0) {
        MVM_free(line_report_store);
    }
    MVM_free(to_fixup);
}

/* Adds instrumented version of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint8 want_coverage) {
    MVMSpeshCode  *sc;
    MVMStaticFrameInstrumentation *ins;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1, 0);
    if (want_coverage)
        instrument_graph(tc, sg);
    else
        instrument_graph_with_breakpoints(tc, sg);
    sc = MVM_spesh_codegen(tc, sg);
    ins = sf->body.instrumentation;
    if (!ins)
        ins = MVM_calloc(1, sizeof(MVMStaticFrameInstrumentation));
    ins->instrumented_bytecode        = sc->bytecode;
    ins->instrumented_handlers        = sc->handlers;
    ins->instrumented_bytecode_size   = sc->bytecode_size;
    ins->uninstrumented_bytecode      = sf->body.bytecode;
    ins->uninstrumented_handlers      = sf->body.handlers;
    ins->uninstrumented_bytecode_size = sf->body.bytecode_size;
    sf->body.instrumentation = ins;
    MVM_spesh_graph_destroy(tc, sg);
    MVM_free(sc);
}


/* Instruments code with per-line logging of code coverage */
static void line_numbers_instrument(MVMThreadContext *tc, MVMStaticFrame *sf, MVMuint8 want_coverage) {
    if (!sf->body.instrumentation || sf->body.bytecode != sf->body.instrumentation->instrumented_bytecode) {
        /* Handle main, non-specialized, bytecode. */
        if (!sf->body.instrumentation || !sf->body.instrumentation->instrumented_bytecode)
            add_instrumentation(tc, sf, want_coverage);
        sf->body.bytecode      = sf->body.instrumentation->instrumented_bytecode;
        if (sf->body.handlers)
            MVM_free(sf->body.handlers);
        sf->body.handlers      = sf->body.instrumentation->instrumented_handlers;
        sf->body.bytecode_size = sf->body.instrumentation->instrumented_bytecode_size;

        /* Throw away any existing specializations. */
        MVM_spesh_candidate_discard_existing(tc, sf);
    }
}

/* Instruments code with per-line logging of code coverage */
void MVM_line_coverage_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    line_numbers_instrument(tc, sf, 1);
}

/* Instruments code with a breakpoint check instruction af every line number change */
void MVM_breakpoint_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    line_numbers_instrument(tc, sf, 0);
}

void MVM_line_coverage_report(MVMThreadContext *tc, MVMString *filename, MVMuint32 line_number, MVMuint16 cache_slot, char *cache) {
    if (tc->instance->coverage_control == 2 || (!tc->instance->coverage_control && cache[cache_slot] == 0)) {
        char *encoded_filename;
        char composed_line[256];

        cache[cache_slot] = 1;

        encoded_filename = MVM_string_utf8_encode_C_string(tc, filename);
        if (snprintf(composed_line, 255, "HIT  %s  %d\n", encoded_filename, line_number) > 0) {
            fputs(composed_line, tc->instance->coverage_log_fh);
        }
        MVM_free(encoded_filename);
    }
}
