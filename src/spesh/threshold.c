#include "moar.h"

/* Choose the threshold for a given static frame before we start applying
 * specialization to it.
 * Lower thresholds for small functions to help grammar token/rule methods
 * be specialized and inlined more easily. */
MVMuint32 MVM_spesh_threshold(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMuint32 bs = sf->body.bytecode_size;
    if (tc->instance->spesh_nodelay)
        return 1;

    /* Special optimization for extremely small functions, likely simple tokens/rules
     * These tiny functions benefit greatly from specialization and inlining
     * but often don't reach high call counts */
    /* Special case for functions less 900 bytes */
    else if (bs < 900) {
        /* Lower threshold for functions in this size range that benefit from inlining */
        return bs / 10;
    }
    /* Original thresholds for larger functions */
    else if (bs <= 2048)
        return 150;
    else if (bs <= 8192)
        return 200;
    else
        return 300;
}
