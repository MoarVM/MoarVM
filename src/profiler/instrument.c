#include "moar.h"

static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    /* Insert entry instruction. */
    MVMSpeshBB *bb         = g->entry->linear_next;
    MVMSpeshIns *enter_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    enter_ins->info        = MVM_op_get_op(MVM_OP_prof_enter);
    MVM_spesh_manipulate_insert_ins(tc, bb, NULL, enter_ins);

    /* Walk the code and insert profile logging instructions as needed. */
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
            case MVM_OP_return_i:
            case MVM_OP_return_n:
            case MVM_OP_return_s:
            case MVM_OP_return_o:
            case MVM_OP_return: {
                /* Log a normal exit prior to returning. */
                MVMSpeshIns *exit_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                exit_ins->info        = MVM_op_get_op(MVM_OP_prof_exit);
                MVM_spesh_manipulate_insert_ins(tc, bb, ins->prev, exit_ins);

                /* If the return instruction is a goto target, move to the
                 * instrumentation instruction. */
                if (ins->annotations) {
                    MVMSpeshAnn *ann      = ins->annotations;
                    MVMSpeshAnn *prev_ann = NULL;
                    while (ann) {
                        if (ann->type == MVM_SPESH_ANN_FH_GOTO) {
                            if (prev_ann)
                                prev_ann->next = ann->next;
                            else
                                ins->annotations = ann->next;
                            exit_ins->annotations = ann;
                            ann->next = NULL;
                            break;
                        }
                        prev_ann = ann;
                        ann = ann->next;
                    }
                }

                break;
            }
            case MVM_OP_create:
            case MVM_OP_clone:
            case MVM_OP_box_i:
            case MVM_OP_box_n:
            case MVM_OP_box_s:
            case MVM_OP_iter:
            case MVM_OP_add_I:
            case MVM_OP_sub_I:
            case MVM_OP_mul_I:
            case MVM_OP_div_I:
            case MVM_OP_mod_I:
            case MVM_OP_neg_I:
            case MVM_OP_abs_I:
            case MVM_OP_bor_I:
            case MVM_OP_bxor_I:
            case MVM_OP_band_I:
            case MVM_OP_bnot_I:
            case MVM_OP_blshift_I:
            case MVM_OP_brshift_I:
            case MVM_OP_pow_I:
            case MVM_OP_gcd_I:
            case MVM_OP_lcm_I:
            case MVM_OP_expmod_I:
            case MVM_OP_rand_I:
            case MVM_OP_coerce_nI:
            case MVM_OP_coerce_sI:
            case MVM_OP_radix_I: {
                /* Insert allocationg logging instruction. */
                MVMSpeshIns *alloc_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
                alloc_ins->info        = MVM_op_get_op(MVM_OP_prof_allocated);
                alloc_ins->operands    = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshOperand));
                alloc_ins->operands[0] = ins->operands[0];
                MVM_spesh_manipulate_insert_ins(tc, bb, ins, alloc_ins);
                break;
            }
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }
}

/* Adds instrumented versions of the unspecialized bytecode. */
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

/* Instruments a static frame for profiling, or uses an existing
 * instrumentation if it exists. */
void MVM_profile_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
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

/* Ensures we're no longer in instrumented code. */
void MVM_profile_ensure_uninstrumented(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (sf->body.bytecode == sf->body.instrumented_bytecode) {
        /* Switch to uninstrumented code. */
        sf->body.bytecode      = sf->body.uninstrumented_bytecode;
        sf->body.handlers      = sf->body.uninstrumented_handlers;
        sf->body.bytecode_size = sf->body.uninstrumented_bytecode_size;

        /* Throw away specializations, which may also be instrumented. */
        sf->body.num_spesh_candidates = 0;
        sf->body.spesh_candidates     = NULL;

        /* XXX For now, due to bugs, disable spesh here. */
        tc->instance->spesh_enabled = 0;
    }
}
