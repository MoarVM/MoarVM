#include "moar.h"

void MVM_spesh_osr(MVMThreadContext *tc) {
    MVMSpeshCandidate *specialized;

    /* Ensure that we are in a position to specialize. */
    if (!tc->cur_frame->caller)
        return;
    if (!tc->cur_frame->params.callsite->is_interned)
        return;

    /* Produce logging spesh candidate. */
    specialized = MVM_spesh_candidate_setup(tc, tc->cur_frame->static_info,
        tc->cur_frame->params.callsite, tc->cur_frame->params.args);
    if (specialized) {
        /* TODO: install. */
    }
}
