#include "moar.h"

/* In some cases, we may have specialized bytecode "on the stack" and need to
 * back out of it, because some assumption it made has been invalidated. */
void MVM_spesh_deopt(MVMThreadContext *tc) {
    /* Walk frames looking for any callers in specialized bytecode. */
    MVMFrame *f = tc->cur_frame->caller;
    while (f) {
        if (f->effective_bytecode != f->static_info->body.bytecode) {
            /* Found one. See if we can find the return address in the deopt
             * table. */
            MVMint32 ret_offset = f->return_address - f->effective_bytecode;
            MVMint32 i;
            for (i = 0; i < f->spesh_cand->num_deopts; i += 2) {
                if (f->spesh_cand->deopts[i + 1] == ret_offset) {
                    /* Found it; switch back to the original code. */
                    f->effective_bytecode    = f->static_info->body.bytecode;
                    f->effective_handlers    = f->static_info->body.handlers;
                    f->return_address        = f->effective_bytecode + f->spesh_cand->deopts[i];
                    f->effective_spesh_slots = NULL;
                    f->spesh_cand            = NULL;
                    break;
                }
            }
        }
        f = f->caller;
    }
}
