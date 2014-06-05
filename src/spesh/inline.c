#include "moar.h"

/* Sees if it will be possible to inline the target code ref, given we could
 * already identify a spesh candidate. Returns NULL if no inlining is possible
 * or a graph ready to be merged if it will be possible. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMCode *target,
                                               MVMSpeshCandidate *cand) {
    MVMSpeshGraph *ig;
    MVMSpeshBB    *bb;

    /* Check bytecode size is below the inline limit. */
    if (target->body.sf->body.bytecode_size > MVM_SPESH_MAX_INLINE_SIZE)
        return NULL;

    /* Build graph from the already-specialized bytecode. */
    ig = MVM_spesh_graph_create_from_cand(tc, target->body.sf, cand);

    /* Traverse graph, looking for anything that might prevent inlining. */
    bb = ig->entry;
    while (bb) {
        MVMSpeshIns *ins = bb->first_ins;
        while (ins) {
            /* Instruction may be marked directly as not being inlinable, in
             * which case we're done. */
            if (ins->info->no_inline)
                goto not_inlinable;

            /* If we have lexical access, make sure it's within the frame. */
            if (ins->info->opcode == MVM_OP_getlex)
                if (ins->operands[1].lex.outers > 0)
                    goto not_inlinable;
            else if (ins->info->opcode == MVM_OP_bindlex)
                if (ins->operands[0].lex.outers > 0)
                    goto not_inlinable;

            ins = ins->next;
        }
        bb = bb->linear_next;
    }

    /* If we found nothing we can't inline, inlining is fine. */
    return ig;

    /* If we can't find a way to inline, we end up here. */
  not_inlinable:
    MVM_spesh_graph_destroy(tc, ig);
    return NULL;
}
