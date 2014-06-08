#include "moar.h"

/* In some cases, we may have specialized bytecode "on the stack" and need to
 * back out of it, because some assumption it made has been invalidated. This
 * file contains implementations of those various forms of de-opt. */

static MVMint32 find_offset_or_die(MVMThreadContext *tc, MVMSpeshCandidate *cand, MVMint32 offset) {
    MVMint32 i;
    for (i = 0; i < cand->num_deopts * 2; i += 2)
        if (cand->deopts[i + 1] == offset)
            return cand->deopts[i + 1];
    MVM_panic(1, "Spesh deopt: failed to uninline; deopt offset %d not found\n", offset);
}

/* If we have to deopt inside of a frame containing inlines, and we're in
 * an inlined frame at the point we hit deopt, we need to undo the inlining
 * by switching all levels of inlined frame out for a bunch of frames that
 * are running the de-optimized code. */
static void uninline(MVMThreadContext *tc, MVMFrame *f, MVMSpeshCandidate *cand,
                     MVMint32 offset, MVMint32 deopt_offset) {
    MVMFrame      *last_uninlined = NULL;
    MVMuint16      last_res_reg;
    MVMReturnType  last_res_type;
    MVMuint32      last_ret_addr;
    MVMint32 i;
    for (i = 0; i < cand->num_inlines; i++) {
        if (offset >= cand->inlines[i].start && offset < cand->inlines[i].end) {
            /* Create the frame. */
            MVMStaticFrame *usf = cand->inlines[i].sf;
            MVMFrame       *uf  = MVM_frame_create_for_deopt(tc, usf, NULL); /* XXX code ref */

            /* Copy the locals and lexicals into place. */
            memcpy(uf->work, f->work + cand->inlines[i].locals_start,
                usf->body.num_locals * sizeof(MVMRegister));
            memcpy(uf->env, f->env + cand->inlines[i].lexicals_start,
                usf->body.num_lexicals * sizeof(MVMRegister));

            /* Did we already uninline a frame? */
            if (last_uninlined) {
                /* Yes; multi-level un-inline. */
                MVM_exception_throw_adhoc(tc, "Multi-level uninline NYI");
            }
            else {
                /* No, so this is where we'll point the interpreter. */
                tc->cur_frame                = uf;
                *(tc->interp_cur_op)         = uf->effective_bytecode + deopt_offset;
                *(tc->interp_bytecode_start) = uf->effective_bytecode;
                *(tc->interp_reg_base)       = uf->work;
                *(tc->interp_cu)             = usf->body.cu;
            }

            /* Update tracking variables for last uninline. Note that we know
             * an inline ends with a goto, which is how we're able to find a
             * return address offset. */
            last_uninlined = uf;
            last_res_reg   = cand->inlines[i].res_reg;
            last_res_type  = cand->inlines[i].res_type;
            last_ret_addr  = *((MVMuint32 *)(f->effective_bytecode + cand->inlines[i].end + 2));
        }
    }
    if (last_uninlined) {
        /* Set return address, which we need to resolve to the deopt'd one. */
        f->return_address = f->static_info->body.bytecode +
            find_offset_or_die(tc, cand, last_ret_addr);

        /* Set result type and register. */
        f->return_type = last_res_type;
        if (last_res_type == MVM_RETURN_VOID)
            f->return_value = NULL;
        else
            f->return_value = f->work + last_res_reg;

        /* Set up inliner as the caller, given we now have a direct inline. */
        last_uninlined->caller = MVM_frame_inc_ref(tc, f);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Failed to uninline in '%s'\n",
            MVM_string_utf8_encode_C_string(tc, f->static_info->body.name));
    }
}

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
                    /* Yes, going to have to re-create the frames; uninline
                     * moves the interpreter, so we can just tweak the last
                     * frame. */
                    uninline(tc, f, f->spesh_cand, deopt_offset, f->spesh_cand->deopts[i]);
                    f->effective_bytecode    = f->static_info->body.bytecode;
                    f->effective_handlers    = f->static_info->body.handlers;
                    f->effective_spesh_slots = NULL;
                    f->spesh_cand            = NULL;
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
