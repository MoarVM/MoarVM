#include "moar.h"

static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry->linear_next;
    MVMuint16 array_slot = 0;

    MVMint32 last_line_number;
    MVMint32 last_filename;

    char *line_report_store = MVM_calloc(g->num_bbs, sizeof(char));
    MVMuint16 allocd_slots = g->num_bbs;

    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        MVMSpeshIns *log_ins;

        MVMBytecodeAnnotation *bbba = MVM_bytecode_resolve_annotation(tc, &g->sf->body, bb->initial_pc);
        MVMuint32 line_number;
        MVMuint32 filename_string_index;
        if (bbba) {
            line_number = bbba->line_number;
            filename_string_index = bbba->filename_string_heap_index;
            MVM_free(bbba);
        } else {
            line_number = -1;
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
        }

        log_ins->operands[3].lit_i64 = (MVMint64)line_report_store;

        MVM_spesh_manipulate_insert_ins(tc, bb, ins, log_ins);

        bb = bb->linear_next;
    }

    if (array_slot == 0) {
        MVM_free(line_report_store);
    } else if (array_slot > g->num_bbs) {
        MVM_panic("we've allocated %d slots for coverage reporting, but we've used up to %d!", g->num_bbs, array_slot);
    }
}

/* Adds instrumented version of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshCode  *sc;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1);
    instrument_graph(tc, sg);
    sc = MVM_spesh_codegen(tc, sg);
    sf->body.instrumented_bytecode        = sc->bytecode;
    sf->body.instrumented_handlers        = sc->handlers;
    sf->body.instrumented_bytecode_size   = sc->bytecode_size;
    sf->body.uninstrumented_bytecode      = sf->body.bytecode;
    sf->body.uninstrumented_handlers      = sf->body.handlers;
    sf->body.uninstrumented_bytecode_size = sf->body.bytecode_size;
    MVM_spesh_graph_destroy(tc, sg);
    MVM_free(sc);
}


/* Instruments code with per-line logging of code coverage */
void MVM_line_coverage_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (sf->body.bytecode != sf->body.instrumented_bytecode) {
        /* Handle main, non-specialized, bytecode. */
        if (!sf->body.instrumented_bytecode)
            add_instrumentation(tc, sf);
        sf->body.bytecode      = sf->body.instrumented_bytecode;
        sf->body.handlers      = sf->body.instrumented_handlers;
        sf->body.bytecode_size = sf->body.instrumented_bytecode_size;

        /* Throw away any specializations; we'll need to reproduce them as
         * instrumented versions. */
        sf->body.num_spesh_candidates = 0;
        sf->body.spesh_candidates     = NULL;
    }
}

void MVM_line_coverage_report(MVMThreadContext *tc, MVMString *filename, MVMuint32 line_number, MVMuint16 cache_slot, char *cache) {
    if (cache[cache_slot] == 0) {
        char *encoded_filename;

        cache[cache_slot] = 1;

        encoded_filename = MVM_string_utf8_encode_C_string(tc, filename);
        fprintf(tc->instance->coverage_log_fh, "HIT  %s  %d\n", encoded_filename, line_number);
        MVM_free(encoded_filename);
    }
}
