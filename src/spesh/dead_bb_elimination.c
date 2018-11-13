#include "moar.h"

static void mark_handler_unreachable(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 index) {
    if (!g->unreachable_handlers)
        g->unreachable_handlers = MVM_spesh_alloc(tc, g, g->num_handlers);
    g->unreachable_handlers[index] = 1;
}

static MVMSpeshBB * linear_next_with_ins(MVMSpeshBB *cur_bb) {
    while (cur_bb) {
        if (cur_bb->first_ins)
            return cur_bb;
        cur_bb = cur_bb->linear_next;
    }
    return NULL;
}

static MVMSpeshBB * linear_prev_with_ins(MVMThreadContext *tc, MVMSpeshGraph *g, MVMSpeshBB *cur_bb) {
    cur_bb = MVM_spesh_graph_linear_prev(tc, g, cur_bb);
    while (cur_bb) {
        if (cur_bb->first_ins)
            return cur_bb;
        cur_bb = MVM_spesh_graph_linear_prev(tc, g, cur_bb);
    }
    return NULL;
}

static void cleanup_dead_bb_instructions(MVMThreadContext *tc, MVMSpeshGraph *g,
                                         MVMSpeshBB *dead_bb, MVMint32 cleanup_facts) {
    MVMSpeshIns *ins = dead_bb->first_ins;
    MVMint8 *frame_handlers_started = MVM_calloc(g->num_handlers, 1);
    while (ins) {
        /* Look over any annotations on the instruction. */
        MVMSpeshAnn *ann = ins->annotations;
        while (ann) {
            MVMSpeshAnn *next_ann = ann->next;
            switch (ann->type) {
                case MVM_SPESH_ANN_INLINE_START:
                    /* If an inline's entrypoint becomes impossible to reach
                     * the whole inline will too. Just mark it as being
                     * unreachable. */
                    g->inlines[ann->data.inline_idx].unreachable = 1;
                    break;
                case MVM_SPESH_ANN_INLINE_END: {
                    /* Move it to the previous basic block, unless we already
                     * found that its start is unreachable. */
                    if (!g->inlines[ann->data.inline_idx].unreachable) {
                        MVMSpeshBB *move_to_bb = linear_prev_with_ins(tc, g, dead_bb);
                        if (move_to_bb) {
                            MVMSpeshIns *move_to_ins = move_to_bb->last_ins;
                            ann->next = move_to_ins->annotations;
                            move_to_ins->annotations = ann;
                        }
                    }
                    break;
                }
                case MVM_SPESH_ANN_FH_START: {
                    /* Move the start to the next basic block if possible. If
                     * not, just mark the handler deleted; its end must be in
                     * this block also. */
                    MVMSpeshBB *move_to_bb = linear_next_with_ins(dead_bb->linear_next);
                    frame_handlers_started[ann->data.frame_handler_index] = 1;
                    if (move_to_bb) {
                        MVMSpeshIns *move_to_ins = move_to_bb->first_ins;
                        ann->next = move_to_ins->annotations;
                        move_to_ins->annotations = ann;
                    }
                    else {
                        mark_handler_unreachable(tc, g, ann->data.frame_handler_index);
                    }
                    break;
                }
                case MVM_SPESH_ANN_FH_END: {
                    /* If we already saw the start, then we'll just mark it as
                     * deleted. */
                    if (frame_handlers_started[ann->data.frame_handler_index]) {
                        mark_handler_unreachable(tc, g, ann->data.frame_handler_index);
                    }

                    /* Otherwise, move it to the end of the previous basic
                     * block (which should always exist). */
                    else {
                        MVMSpeshBB *move_to_bb = linear_prev_with_ins(tc, g, dead_bb);
                        if (move_to_bb) {
                            MVMSpeshIns *move_to_ins = move_to_bb->last_ins;
                            ann->next = move_to_ins->annotations;
                            move_to_ins->annotations = ann;
                        }
                    }   
                    break;
                }
                case MVM_SPESH_ANN_FH_GOTO:
                    mark_handler_unreachable(tc, g, ann->data.frame_handler_index);
                    break;
            }
            ann = next_ann;
        }
        if (cleanup_facts)
            MVM_spesh_manipulate_cleanup_ins_deps(tc, g, ins);
        ins = ins->next;
    }
    dead_bb->first_ins = NULL;
    dead_bb->last_ins = NULL;
    MVM_free(frame_handlers_started);
}

static void mark_bb_seen(MVMThreadContext *tc, MVMSpeshBB *bb, MVMint8 *seen) {
    if (!seen[bb->idx]) {
        MVMuint16 i;
        seen[bb->idx] = 1;
        for (i = 0; i < bb->num_succ; i++)
            mark_bb_seen(tc, bb->succ[i], seen);
    }
}

/* Eliminates dead basic blocks, optionally cleaning up facts. (In the case
 * this is called during spesh graph construction, the facts do not yet
 * exist). */
void MVM_spesh_eliminate_dead_bbs(MVMThreadContext *tc, MVMSpeshGraph *g, MVMint32 update_facts) {
    MVMSpeshBB *cur_bb;

    /* First pass: mark every basic block that is reachable from the
     * entrypoint. */
    MVMint32  orig_bbs = g->num_bbs;
    MVMint8  *seen = MVM_calloc(1, g->num_bbs);
    mark_bb_seen(tc, g->entry, seen);

    /* Second pass: remove dead BBs from the graph. */
    cur_bb = g->entry;
    while (cur_bb && cur_bb->linear_next) {
        MVMSpeshBB *death_cand = cur_bb->linear_next;
        if (!seen[death_cand->idx]) {
            cleanup_dead_bb_instructions(tc, g, death_cand, update_facts);
            death_cand->dead = 1;
            g->num_bbs--;
            cur_bb->linear_next = cur_bb->linear_next->linear_next;
        }
        else {
            cur_bb = cur_bb->linear_next;
        }
    }
    MVM_free(seen);

    /* Re-number BBs so we get sequential ordering again. */
    if (g->num_bbs != orig_bbs) {
        MVMint32    new_idx  = 0;
        MVMSpeshBB *cur_bb   = g->entry;
        while (cur_bb) {
            cur_bb->idx = new_idx;
            new_idx++;
            cur_bb = cur_bb->linear_next;
        }
    }
}
