#include "moar.h"

/* Sees if it will be possible to inline the target code ref, given we could
 * already identify a spesh candidate. Returns NULL if no inlining is possible
 * or a graph ready to be merged if it will be possible. */
MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMCode *target,
                                               MVMSpeshCandidate *cand) {
    MVMSpeshGraph *ig;

    /* Check bytecode size is below the inline limit. */
    if (target->body.sf->body.bytecode_size > MVM_SPESH_MAX_INLINE_SIZE)
        return NULL;

    /* Build graph from the already-specialized bytecode. */
    ig = MVM_spesh_graph_create_from_cand(tc, target->body.sf, cand);

    return ig;
}
