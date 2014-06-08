#include "moar.h"

/* In some cases, we may have specialized bytecode "on the stack" and need to
 * back out of it, because some assumption it made has been invalidated. This
 * file contains implementations of those various forms of de-opt. */
 
/* De-optimizes the currently executing frame, provided it is specialized and
 * at a valid de-optimization point. Typically used when a guard fails. */
void MVM_spesh_deopt_one(MVMThreadContext *tc) {
    MVMFrame *f = tc->cur_frame;
    if (f->effective_bytecode != f->static_info->body.bytecode) {
        MVMint32 deopt_offset = *(tc->interp_cur_op) - f->effective_bytecode;
        MVMint32 i;
        for (i = 0; i < f->spesh_cand->num_deopts * 2; i += 2) {
            if (f->spesh_cand->deopts[i + 1] == deopt_offset) {
                /* Found it; are we in an inline? */
                MVMSpeshInline *inlines = f->spesh_cand->inlines;
                if (inlines) {
                    /* Yes, going to have to re-create the frames. */
                    MVM_exception_throw_adhoc(tc,
                        "Don't know how to deopt_one from inline yet\n");
                }
                else {
                    /* No inlining; simple case. Switch back to the original code. */
                    f->effective_bytecode        = f->static_info->body.bytecode;
                    f->effective_handlers        = f->static_info->body.handlers;
                    *(tc->interp_cur_op)         = f->effective_bytecode + f->spesh_cand->deopts[i];
                    *(tc->interp_bytecode_start) = f->effective_bytecode;
                    f->effective_spesh_slots     = NULL;
                    f->spesh_cand                = NULL;
                    /*printf("did deopt_one for %s (%i)\n",
                        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
                        i / 2);*/
                }
                return;
            }
        }
    }
    MVM_exception_throw_adhoc(tc, "deopt_one failed for %s",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name));
}

/* De-optimizes all specialized frames on the call stack. Used when a change
 * is made the could invalidate all kinds of assumptions all over the place
 * (such as a mix-in). */
void MVM_spesh_deopt_all(MVMThreadContext *tc) {
    /* Walk frames looking for any callers in specialized bytecode. */
    MVMFrame *f = tc->cur_frame->caller;
    while (f) {
        if (f->effective_bytecode != f->static_info->body.bytecode && f->spesh_log_idx < 0) {
            /* Found one. See if we can find the return address in the deopt
             * table. */
            MVMint32 ret_offset = f->return_address - f->effective_bytecode;
            MVMint32 i;
            for (i = 0; i < f->spesh_cand->num_deopts * 2; i += 2) {
                if (f->spesh_cand->deopts[i + 1] == ret_offset) {
                    /* Found it; are we in an inline? */
                    MVMSpeshInline *inlines = f->spesh_cand->inlines;
                    if (inlines) {
                        /* Yes, going to have to re-create the frames. */
                        MVM_exception_throw_adhoc(tc,
                            "Don't know how to deopt_all from inline yet\n");
                    }
                    else {
                        /* Found it; switch back to the original code. */
                        f->effective_bytecode    = f->static_info->body.bytecode;
                        f->effective_handlers    = f->static_info->body.handlers;
                        f->return_address        = f->effective_bytecode + f->spesh_cand->deopts[i];
                        f->effective_spesh_slots = NULL;
                        f->spesh_cand            = NULL;
                    }
                    break;
                }
            }
        }
        f = f->caller;
    }
}
