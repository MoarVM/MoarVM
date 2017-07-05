#include "moar.h"

/* Marks any objects held by an argument guard. */
void MVM_spesh_arg_guard_gc_mark(MVMThreadContext *tc, MVMSpeshArgGuard *ag,
                                 MVMGCWorklist *worklist) {
    if (ag) {
        MVMuint32 i;
        for (i = 0; i < ag->used_nodes; i++) {
            switch (ag->nodes[i].op) {
                case MVM_SPESH_GUARD_OP_STABLE_CONC:
                case MVM_SPESH_GUARD_OP_STABLE_TYPE:
                    MVM_gc_worklist_add(tc, worklist, &(ag->nodes[i].st));
                    break;
            }
        }
    }
}

/* Frees the memory associated with an argument guard. If `safe` is set to a
 * non-zero value then the memory is freed at the next safepoint. If it is set
 * to zero, the memory is freed immediately. */
void MVM_spesh_arg_guard_destroy(MVMThreadContext *tc, MVMSpeshArgGuard *ag, MVMuint32 safe) {
    if (ag) {
        size_t total_size = sizeof(MVMSpeshArgGuard) +
            ag->num_nodes * sizeof(MVMSpeshArgGuardNode);
        if (safe)
            MVM_fixed_size_free(tc, tc->instance->fsa, total_size, ag);
        else
            MVM_fixed_size_free_at_safepoint(tc, tc->instance->fsa, total_size, ag);
    }
}
