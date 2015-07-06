#include "moar.h"

/* In some cases, we may have specialized bytecode "on the stack" and need to
 * back out of it, because some assumption it made has been invalidated. This
 * file contains implementations of those various forms of de-opt. */

/* If we have to deopt inside of a frame containing inlines, and we're in
 * an inlined frame at the point we hit deopt, we need to undo the inlining
 * by switching all levels of inlined frame out for a bunch of frames that
 * are running the de-optimized code. We may, of course, be in the original,
 * non-inline, bit of the code - in which case we've nothing to do. */
static void uninline(MVMThreadContext *tc, MVMFrame *f, MVMSpeshCandidate *cand,
                     MVMint32 offset, MVMint32 deopt_offset, MVMFrame *callee) {
    MVMFrame      *last_uninlined = NULL;
    MVMuint16      last_res_reg;
    MVMReturnType  last_res_type;
    MVMuint32      last_return_deopt_idx;
    MVMint32 i;
    for (i = 0; i < cand->num_inlines; i++) {
        if (offset >= cand->inlines[i].start && offset < cand->inlines[i].end) {
            /* Create the frame. */
            MVMCode        *ucode = cand->inlines[i].code;
            MVMStaticFrame *usf   = ucode->body.sf;
            MVMFrame       *uf    = MVM_frame_create_for_deopt(tc, usf, ucode);
            /*fprintf(stderr, "Recreated frame '%s' (cuid '%s')\n",
                MVM_string_utf8_encode_C_string(tc, usf->body.name),
                MVM_string_utf8_encode_C_string(tc, usf->body.cuuid));*/

            /* Copy the locals and lexicals into place. */
            memcpy(uf->work, f->work + cand->inlines[i].locals_start,
                usf->body.num_locals * sizeof(MVMRegister));
            memcpy(uf->env, f->env + cand->inlines[i].lexicals_start,
                usf->body.num_lexicals * sizeof(MVMRegister));

            /* Did we already uninline a frame? */
            if (last_uninlined) {
                /* Yes; multi-level un-inline. Switch it back to deopt'd
                 * code. */
                uf->effective_bytecode    = uf->static_info->body.bytecode;
                uf->effective_handlers    = uf->static_info->body.handlers;
                uf->effective_spesh_slots = NULL;
                uf->spesh_cand            = NULL;

                /* Set up the return location. */
                uf->return_address = uf->static_info->body.bytecode +
                    cand->deopts[2 * last_return_deopt_idx];

                /* Set result type and register. */
                uf->return_type = last_res_type;
                if (last_res_type == MVM_RETURN_VOID)
                    uf->return_value = NULL;
                else
                    uf->return_value = uf->work + last_res_reg;

                /* Set up last uninlined's caller to us. */
                last_uninlined->caller = MVM_frame_inc_ref(tc, uf);
            }
            else {
                /* First uninlined frame. Are we in the middle of the call
                 * stack (and thus in deopt_all)? */
                if (callee) {
                    /* Tweak the callee's caller to the uninlined frame, not
                     * the frame holding the inlinings. */
                    MVMFrame *orig_caller = callee->caller;
                    callee->caller = MVM_frame_inc_ref(tc, uf);
                    MVM_frame_dec_ref(tc, orig_caller);

                    /* Copy over the return location. */
                    uf->return_address = uf->effective_bytecode +
                        deopt_offset;

                    /* Set result type and register. */
                    uf->return_type = f->return_type;
                    if (uf->return_type == MVM_RETURN_VOID) {
                        uf->return_value = NULL;
                    }
                    else {
                        MVMuint16 orig_reg = (MVMuint16)(f->return_value - f->work);
                        MVMuint16 ret_reg  = orig_reg - cand->inlines[i].locals_start;
                        uf->return_value = uf->work + ret_reg;
                    }
                }
                else {
                    /* No, it's the deopt_one case, so this is where we'll point
                     * the interpreter. */
                    tc->cur_frame                = uf;
                    *(tc->interp_cur_op)         = uf->effective_bytecode + deopt_offset;
                    *(tc->interp_bytecode_start) = uf->effective_bytecode;
                    *(tc->interp_reg_base)       = uf->work;
                    *(tc->interp_cu)             = usf->body.cu;
                }
            }

            /* Update tracking variables for last uninline. Note that we know
             * an inline ends with a goto, which is how we're able to find a
             * return address offset. */
            last_uninlined        = uf;
            last_res_reg          = cand->inlines[i].res_reg;
            last_res_type         = cand->inlines[i].res_type;
            last_return_deopt_idx = cand->inlines[i].return_deopt_idx;
        }
    }
    if (last_uninlined) {
        /* Set return address, which we need to resolve to the deopt'd one. */
        f->return_address = f->static_info->body.bytecode +
            cand->deopts[2 * last_return_deopt_idx];

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
        /* Weren't in an inline after all. What kind of deopt? */
        if (callee) {
            /* Deopt all. Move return address. */
            f->return_address = f->effective_bytecode + deopt_offset;
        }
        else {
            /* Deopt one. Move interpreter. */
            *(tc->interp_cur_op)         = f->static_info->body.bytecode + deopt_offset;
            *(tc->interp_bytecode_start) = f->static_info->body.bytecode;
        }
    }
}

static MVMint32 find_deopt_target(MVMThreadContext *tc, MVMFrame *f, MVMint32 deopt_offset) {
    MVMint32 i;
    for (i = 0; i < f->spesh_cand->num_deopts * 2; i += 2) {
        if (f->spesh_cand->deopts[i + 1] == deopt_offset) {
            return f->spesh_cand->deopts[i];
        }
    }
    MVM_oops(tc, "find_deopt_target failed for %s (%s)",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
}

static void deopt_frame(MVMThreadContext *tc, MVMFrame *f, MVMint32 deopt_offset, MVMint32 deopt_target) {
    /* Found it; are we in an inline? */
    MVMSpeshInline *inlines = f->spesh_cand->inlines;
    if (inlines) {
        /* Yes, going to have to re-create the frames; uninline
         * moves the interpreter, so we can just tweak the last
         * frame. */
        uninline(tc, f, f->spesh_cand, deopt_offset, deopt_target, NULL);
        f->effective_bytecode    = f->static_info->body.bytecode;
        f->effective_handlers    = f->static_info->body.handlers;
        f->effective_spesh_slots = NULL;
        f->spesh_cand            = NULL;
        /*fprintf(stderr, "Completed deopt_one in '%s' (cuid '%s'), idx = %i, with uninlining\n",
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid),
          i / 2);*/
    }
    else {
        /* No inlining; simple case. Switch back to the original code. */
        f->effective_bytecode        = f->static_info->body.bytecode;
        f->effective_handlers        = f->static_info->body.handlers;
        *(tc->interp_cur_op)         = f->effective_bytecode + deopt_target;
        *(tc->interp_bytecode_start) = f->effective_bytecode;
        f->effective_spesh_slots     = NULL;
        f->spesh_cand                = NULL;
        /*fprintf(stderr, "Completed deopt_one in '%s' (cuid '%s'), idx = %i\n",
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid),
          i / 2);*/
    }

}

/* De-optimizes the currently executing frame, provided it is specialized and
 * at a valid de-optimization point. Typically used when a guard fails. */
void MVM_spesh_deopt_one(MVMThreadContext *tc) {
    MVMFrame *f = tc->cur_frame;
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_one(tc);
    /*fprintf(stderr, "deopt_one requested in frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));*/
    if (f->effective_bytecode != f->static_info->body.bytecode) {
        MVMint32 deopt_offset = *(tc->interp_cur_op) - f->effective_bytecode;
        MVMint32 deopt_target = find_deopt_target(tc, f, deopt_offset);
        deopt_frame(tc, tc->cur_frame, deopt_offset, deopt_target);
    }
    else {
        MVM_oops(tc, "deopt_one failed for %s (%s)",
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
    }
}

/* De-optimizes the current frame by directly specifying the addresses */
void MVM_spesh_deopt_one_direct(MVMThreadContext *tc, MVMint32 deopt_offset,
                                MVMint32 deopt_target) {
    MVMFrame *f = tc->cur_frame;
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_one(tc);
    if (f->effective_bytecode != f->static_info->body.bytecode) {
        deopt_frame(tc, tc->cur_frame, deopt_offset, deopt_target);
    } else {
        MVM_oops(tc, "deopt_one_direct failed for %s (%s)",
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
    }
    
}

/* De-optimizes all specialized frames on the call stack. Used when a change
 * is made the could invalidate all kinds of assumptions all over the place
 * (such as a mix-in). */
void MVM_spesh_deopt_all(MVMThreadContext *tc) {
    /* Walk frames looking for any callers in specialized bytecode. */
    MVMFrame *l = tc->cur_frame;
    MVMFrame *f = tc->cur_frame->caller;
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_all(tc);
    while (f) {
        if (f->effective_bytecode != f->static_info->body.bytecode && f->spesh_log_idx < 0) {
            /* Found one. Is it JITted code? */
            if (f->spesh_cand->jitcode && f->jit_entry_label) {
                MVMint32 num_deopts = f->spesh_cand->jitcode->num_deopts;
                MVMJitDeopt *deopts = f->spesh_cand->jitcode->deopts;
                void       **labels = f->spesh_cand->jitcode->labels;
                MVMint32 i;
                for (i = 0; i < num_deopts; i++) {
                    if (labels[deopts[i].label] == f->jit_entry_label) {
                        /*
                        fprintf(stderr, "Found deopt label for JIT (%d) (label %d idx %d)\n", i,
                                deopts[i].label, deopts[i].idx);
                        */
                        /* Resolve offset and target. */
                        MVMint32 deopt_idx    = deopts[i].idx;
                        MVMint32 deopt_offset = f->spesh_cand->deopts[2 * deopt_idx + 1];
                        MVMint32 deopt_target = f->spesh_cand->deopts[2 * deopt_idx];

                        /* Switch frame itself back to the original code. */
                        f->effective_bytecode    = f->static_info->body.bytecode;
                        f->effective_handlers    = f->static_info->body.handlers;

                        /* Re-create any frames needed if we're in an inline; if not,
                        * just update return address. */
                        if (f->spesh_cand->inlines)
                            uninline(tc, f, f->spesh_cand, deopt_offset, deopt_target, l);
                        else
                            f->return_address = f->effective_bytecode + deopt_target;

                        /* No spesh cand/slots needed now. */
                        f->effective_spesh_slots = NULL;
                        f->spesh_cand            = NULL;
                        f->jit_entry_label       = NULL;

                        break;
                    }
                }
                /*
                if (i == num_deopts)
                    fprintf(stderr, "JIT: can't find deopt all idx");
                */
            }

            else {
                /* Not JITted; see if we can find the return address in the deopt table. */
                MVMint32 ret_offset = f->return_address - f->effective_bytecode;
                MVMint32 i;
                for (i = 0; i < f->spesh_cand->num_deopts * 2; i += 2) {
                    if (f->spesh_cand->deopts[i + 1] == ret_offset) {
                        /* Switch frame itself back to the original code. */
                        f->effective_bytecode    = f->static_info->body.bytecode;
                        f->effective_handlers    = f->static_info->body.handlers;

                        /* Re-create any frames needed if we're in an inline; if not,
                        * just update return address. */
                        if (f->spesh_cand->inlines)
                            uninline(tc, f, f->spesh_cand, ret_offset, f->spesh_cand->deopts[i], l);
                        else
                            f->return_address = f->effective_bytecode + f->spesh_cand->deopts[i];

                        /* No spesh cand/slots needed now. */
                        f->effective_spesh_slots = NULL;
                        f->spesh_cand            = NULL;

                        break;
                    }
                }
            }
        }
        l = f;
        f = f->caller;
    }
}
