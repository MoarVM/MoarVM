#include "moar.h"

/* Walk graph and insert write check instructions. */
static void prepend_ctw_check(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *bb,
                              const MVMSpeshIns *before_ins, MVMSpeshOperand check_reg,
                              MVMint16 guilty) {
    MVMSpeshIns *ctw_ins = MVM_spesh_alloc(tc, g, sizeof(MVMSpeshIns));
    ctw_ins->info        = MVM_op_get_op(MVM_OP_ctw_check);
    ctw_ins->operands    = MVM_spesh_alloc(tc, g, 2 * sizeof(MVMSpeshOperand));
    ctw_ins->operands[0] = check_reg;
    ctw_ins->operands[1].lit_i16 = guilty;
    MVM_spesh_manipulate_insert_ins(tc, bb, before_ins->prev, ctw_ins);
}
static void instrument_graph(MVMThreadContext *tc, MVMSpeshGraph *g) {
    MVMSpeshBB *bb = g->entry->linear_next;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            switch (ins->info->opcode) {
                case MVM_OP_rebless:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_REBLESS);
                case MVM_OP_bindattr_i:
                case MVM_OP_bindattr_n:
                case MVM_OP_bindattr_s:
                case MVM_OP_bindattr_o:
                case MVM_OP_bindattrs_i:
                case MVM_OP_bindattrs_n:
                case MVM_OP_bindattrs_s:
                case MVM_OP_bindattrs_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_BIND_ATTR);
                    break;
                case MVM_OP_bindpos_i:
                case MVM_OP_bindpos_n:
                case MVM_OP_bindpos_s:
                case MVM_OP_bindpos_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_BIND_POS);
                    break;
                case MVM_OP_push_i:
                case MVM_OP_push_n:
                case MVM_OP_push_s:
                case MVM_OP_push_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_PUSH);
                    break;
                case MVM_OP_pop_i:
                case MVM_OP_pop_n:
                case MVM_OP_pop_s:
                case MVM_OP_pop_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[1], MVM_CTW_POP);
                    break;
                case MVM_OP_shift_i:
                case MVM_OP_shift_n:
                case MVM_OP_shift_s:
                case MVM_OP_shift_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[1], MVM_CTW_SHIFT);
                    break;
                case MVM_OP_unshift_i:
                case MVM_OP_unshift_n:
                case MVM_OP_unshift_s:
                case MVM_OP_unshift_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_UNSHIFT);
                    break;
                case MVM_OP_splice:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_SPLICE);
                    break;
                case MVM_OP_bindkey_i:
                case MVM_OP_bindkey_n:
                case MVM_OP_bindkey_s:
                case MVM_OP_bindkey_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_BIND_KEY);
                    break;
                case MVM_OP_deletekey:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_DELETE_KEY);
                    break;
                case MVM_OP_assign:
                case MVM_OP_assignunchecked:
                case MVM_OP_assign_i:
                case MVM_OP_assign_n:
                case MVM_OP_assign_s:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_ASSIGN);
                    break;
                case MVM_OP_bindpos2d_i:
                case MVM_OP_bindpos2d_n:
                case MVM_OP_bindpos2d_s:
                case MVM_OP_bindpos2d_o:
                case MVM_OP_bindpos3d_i:
                case MVM_OP_bindpos3d_n:
                case MVM_OP_bindpos3d_s:
                case MVM_OP_bindpos3d_o:
                case MVM_OP_bindposnd_i:
                case MVM_OP_bindposnd_n:
                case MVM_OP_bindposnd_s:
                case MVM_OP_bindposnd_o:
                    prepend_ctw_check(tc, g, bb, ins, ins->operands[0], MVM_CTW_BIND_POS);
                    break;
                default:
                    break;
            }
            ins = ins->next;
        }
        bb = bb->linear_next;
    }
}

/* Adds instrumented version of the unspecialized bytecode. */
static void add_instrumentation(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMSpeshCode  *sc;
    MVMStaticFrameInstrumentation *ins;
    MVMSpeshGraph *sg = MVM_spesh_graph_create(tc, sf, 1, 0);
    instrument_graph(tc, sg);
    sc = MVM_spesh_codegen(tc, sg);
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

/* Instruments code with detection and reporting of cross-thread writes. */
void MVM_cross_thread_write_instrument(MVMThreadContext *tc, MVMStaticFrame *sf) {
    if (!sf->body.instrumentation || sf->body.bytecode != sf->body.instrumentation->instrumented_bytecode) {
        /* Handle main, non-specialized, bytecode. */
        if (!sf->body.instrumentation)
            add_instrumentation(tc, sf);
        sf->body.bytecode      = sf->body.instrumentation->instrumented_bytecode;
        sf->body.handlers      = sf->body.instrumentation->instrumented_handlers;
        sf->body.bytecode_size = sf->body.instrumentation->instrumented_bytecode_size;

        /* Throw away any specializations; we'll need to reproduce them as
         * instrumented versions. */
        sf->body.num_spesh_candidates = 0;
        sf->body.spesh_candidates     = NULL;
    }
}

/* Filter out some special cases to reduce noise. */
static MVMint64 filtered_out(MVMThreadContext *tc, const MVMObject *written) {
    /* If we're holding locks, exclude by default (unless we were asked to
     * also include these). */
    if (tc->num_locks && !tc->instance->cross_thread_write_logging_include_locked)
        return 1;

    /* Operations on a concurrent queue are fine 'cus it's concurrent. */
    if (REPR(written)->ID == MVM_REPR_ID_ConcBlockingQueue)
        return 1;

    /* Otherwise, may be relevant. */
    return 0;
}

/* Squeal if the target of the write wasn't allocated by us. */
void MVM_cross_thread_write_check(MVMThreadContext *tc, const MVMObject *written, MVMint16 guilty) {
    if (written->header.owner != tc->thread_id && !filtered_out(tc, written)) {
        char *guilty_desc = "did something to";
        switch (guilty) {
            case MVM_CTW_BIND_ATTR:
                guilty_desc = "bound to an attribute of";
                break;
            case MVM_CTW_BIND_POS:
                guilty_desc = "bound to an array slot of";
                break;
            case MVM_CTW_PUSH:
                guilty_desc = "pushed to";
                break;
            case MVM_CTW_POP:
                guilty_desc = "popped";
                break;
            case MVM_CTW_SHIFT:
                guilty_desc = "shifted";
                break;
            case MVM_CTW_UNSHIFT:
                guilty_desc = "unshifted to";
                break;
            case MVM_CTW_SPLICE:
                guilty_desc = "spliced";
                break;
            case MVM_CTW_BIND_KEY:
                guilty_desc = "bound to a hash key of";
                break;
            case MVM_CTW_DELETE_KEY:
                guilty_desc = "deleted a hash key of";
                break;
            case MVM_CTW_ASSIGN:
                guilty_desc = "assigned to";
                break;
            case MVM_CTW_REBLESS:
                guilty_desc = "reblessed";
                break;
        }
        uv_mutex_lock(&(tc->instance->mutex_cross_thread_write_logging));
        fprintf(stderr, "Thread %d %s an object allocated by thread %d\n",
            tc->thread_id, guilty_desc, written->header.owner);
        MVM_dump_backtrace(tc);
        fprintf(stderr, "\n");
        uv_mutex_unlock(&(tc->instance->mutex_cross_thread_write_logging));
    }
}
