#include "moar.h"

/* In some cases, we may have specialized bytecode "on the stack" and need to
 * back out of it, because some assumption it made has been invalidated. This
 * file contains implementations of those various forms of de-opt. */

#define MVM_LOG_DEOPTS 0

/* Uninlining can invalidate what the dynlex cache points to, so we'll
 * clear it in various caches. */
MVM_STATIC_INLINE void clear_dynlex_cache(MVMThreadContext *tc, MVMFrame *f) {
    MVMFrameExtra *e = f->extra;
    if (e) {
        e->dynlex_cache_name = NULL;
        e->dynlex_cache_reg = NULL;
    }
}

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
            MVMCode        *ucode = (MVMCode *)f->work[cand->inlines[i].code_ref_reg].o;
            MVMStaticFrame *usf   = cand->inlines[i].sf;
            MVMFrame       *uf;
            if (REPR(ucode)->ID != MVM_REPR_ID_MVMCode)
                MVM_panic(1, "Deopt: did not find code object when uninlining");
            MVMROOT(tc, f, {
            MVMROOT(tc, callee, {
            MVMROOT(tc, last_uninlined, {
            MVMROOT(tc, usf, {
                uf = MVM_frame_create_for_deopt(tc, usf, ucode);
            });
            });
            });
            });
#if MVM_LOG_DEOPTS
            fprintf(stderr, "Recreated frame '%s' (cuid '%s')\n",
                MVM_string_utf8_encode_C_string(tc, usf->body.name),
                MVM_string_utf8_encode_C_string(tc, usf->body.cuuid));
#endif

            /* Copy the locals and lexicals into place. */
            if (usf->body.num_locals)
                memcpy(uf->work, f->work + cand->inlines[i].locals_start,
                    usf->body.num_locals * sizeof(MVMRegister));
            if (usf->body.num_lexicals)
                memcpy(uf->env, f->env + cand->inlines[i].lexicals_start,
                    usf->body.num_lexicals * sizeof(MVMRegister));

            /* Store the named argument used bit field, since if we deopt in
             * argument handling code we may have missed some. */
            if (cand->inlines[i].deopt_named_used_bit_field)
                uf->params.named_used.bit_field = cand->inlines[i].deopt_named_used_bit_field;

            /* Did we already uninline a frame? */
            if (last_uninlined) {
                /* Yes; multi-level un-inline. Switch it back to deopt'd
                 * code. */
                uf->effective_spesh_slots = NULL;
                uf->spesh_cand            = NULL;

                /* Set up the return location. */
                uf->return_address = usf->body.bytecode +
                    cand->deopts[2 * last_return_deopt_idx];

                /* Set result type and register. */
                uf->return_type = last_res_type;
                if (last_res_type == MVM_RETURN_VOID)
                    uf->return_value = NULL;
                else
                    uf->return_value = uf->work + last_res_reg;

                /* Set up last uninlined's caller to us. */
                MVM_ASSIGN_REF(tc, &(last_uninlined->header), last_uninlined->caller, uf);
            }
            else {
                /* First uninlined frame. Are we in the middle of the call
                 * stack (and thus in deopt_all)? */
                if (callee) {
                    /* Tweak the callee's caller to the uninlined frame, not
                     * the frame holding the inlinings. */
                    MVM_ASSIGN_REF(tc, &(callee->header), callee->caller, uf);

                    /* Copy over the return location. */
                    uf->return_address = usf->body.bytecode + deopt_offset;

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
                    tc->current_frame_nr         = uf->sequence_nr;
                    *(tc->interp_cur_op)         = usf->body.bytecode + deopt_offset;
                    *(tc->interp_bytecode_start) = usf->body.bytecode;
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
        MVM_ASSIGN_REF(tc, &(last_uninlined->header), last_uninlined->caller, f);
    }
    else {
        /* Weren't in an inline after all. What kind of deopt? */
        if (callee) {
            /* Deopt all. Move return address. */
            f->return_address = f->static_info->body.bytecode + deopt_offset;
        }
        else {
            /* Deopt one. Move interpreter. */
            *(tc->interp_cur_op)         = f->static_info->body.bytecode + deopt_offset;
            *(tc->interp_bytecode_start) = f->static_info->body.bytecode;
        }
    }
}

static void deopt_named_args_used(MVMThreadContext *tc, MVMFrame *f) {
    if (f->spesh_cand->deopt_named_used_bit_field)
        f->params.named_used.bit_field = f->spesh_cand->deopt_named_used_bit_field;
}

static void deopt_frame(MVMThreadContext *tc, MVMFrame *f, MVMint32 deopt_offset, MVMint32 deopt_target) {
    /* Found it; are we in an inline? */
    MVMSpeshInline *inlines = f->spesh_cand->inlines;
    deopt_named_args_used(tc, f);
    if (inlines) {
        /* Yes, going to have to re-create the frames; uninline
         * moves the interpreter, so we can just tweak the last
         * frame. For the moment, uninlining creates its frames
         * on the heap, so we'll force the current call stack to
         * the heap to preserve the "no heap -> stack pointers"
         * invariant. */
        f = MVM_frame_force_to_heap(tc, f);
        MVMROOT(tc, f, {
            uninline(tc, f, f->spesh_cand, deopt_offset, deopt_target, NULL);
        });
        f->effective_spesh_slots = NULL;
        f->spesh_cand            = NULL;
#if MVM_LOG_DEOPTS
        fprintf(stderr, "Completed deopt_one in '%s' (cuid '%s') with uninlining\n",
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    }
    else {
        /* No inlining; simple case. Switch back to the original code. */
        *(tc->interp_cur_op)         = f->static_info->body.bytecode + deopt_target;
        *(tc->interp_bytecode_start) = f->static_info->body.bytecode;
        f->effective_spesh_slots     = NULL;
        f->spesh_cand                = NULL;
#if MVM_LOG_DEOPTS
        fprintf(stderr, "Completed deopt_one in '%s' (cuid '%s')\n",
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
          MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    }

}

/* De-optimizes the currently executing frame, provided it is specialized and
 * at a valid de-optimization point. Typically used when a guard fails. */
void MVM_spesh_deopt_one(MVMThreadContext *tc, MVMuint32 deopt_target) {
    MVMFrame *f = tc->cur_frame;
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_one(tc);
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Deopt one requested by interpreter in frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    clear_dynlex_cache(tc, f);
    if (f->spesh_cand) {
        MVMuint32 deopt_offset = *(tc->interp_cur_op) - f->spesh_cand->bytecode;
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Will deopt %u -> %u\n", deopt_offset, deopt_target);
#endif
        deopt_frame(tc, tc->cur_frame, deopt_offset, deopt_target);
    }
    else {
        MVM_oops(tc, "deopt_one failed for %s (%s)",
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
    }
}

/* De-optimizes the current frame by directly specifying the addresses */
void MVM_spesh_deopt_one_direct(MVMThreadContext *tc, MVMuint32 deopt_offset,
                                MVMuint32 deopt_target) {
    MVMFrame *f = tc->cur_frame;
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Deopt one requested by JIT in frame '%s' (cuid '%s') (%u -> %u)\n",
        MVM_string_utf8_encode_C_string(tc, f->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, f->static_info->body.cuuid),
        deopt_offset, deopt_target);
#endif
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_one(tc);
    clear_dynlex_cache(tc, f);
    deopt_frame(tc, tc->cur_frame, deopt_offset, deopt_target);
}

/* De-optimizes all specialized frames on the call stack. Used when a change
 * is made the could invalidate all kinds of assumptions all over the place
 * (such as a mix-in). */
void MVM_spesh_deopt_all(MVMThreadContext *tc) {
    /* Walk frames looking for any callers in specialized bytecode. */
    MVMFrame *l = MVM_frame_force_to_heap(tc, tc->cur_frame);
    MVMFrame *f = tc->cur_frame->caller;
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Deopt all requested in frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, l->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, l->static_info->body.cuuid));
#endif
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_all(tc);
    while (f) {
        clear_dynlex_cache(tc, f);
        if (f->spesh_cand) {
            /* Found one. Is it JITted code? */
            if (f->spesh_cand->jitcode && f->jit_entry_label) {
                MVMint32 num_deopts = f->spesh_cand->jitcode->num_deopts;
                MVMJitDeopt *deopts = f->spesh_cand->jitcode->deopts;
                void       **labels = f->spesh_cand->jitcode->labels;
                MVMint32 i;
                for (i = 0; i < num_deopts; i++) {
                    if (labels[deopts[i].label] == f->jit_entry_label) {
                        /* Resolve offset and target. */
                        MVMint32 deopt_idx    = deopts[i].idx;
                        MVMint32 deopt_offset = f->spesh_cand->deopts[2 * deopt_idx + 1];
                        MVMint32 deopt_target = f->spesh_cand->deopts[2 * deopt_idx];
#if MVM_LOG_DEOPTS
                        fprintf(stderr, "Found deopt label for JIT (%d) (label %d idx %d)\n", i,
                                deopts[i].label, deopts[i].idx);
#endif

                        /* Re-create any frames needed if we're in an inline; if not,
                        * just update return address. */
                        if (f->spesh_cand->inlines) {
                            MVMROOT(tc, f, {
                            MVMROOT(tc, l, {
                                uninline(tc, f, f->spesh_cand, deopt_offset, deopt_target, l);
                            });
                            });
                        }
                        else {
                            f->return_address = f->static_info->body.bytecode + deopt_target;
                        }

                        /* No spesh cand/slots needed now. */
                        deopt_named_args_used(tc, f);
                        f->effective_spesh_slots = NULL;
                        f->spesh_cand            = NULL;
                        f->jit_entry_label       = NULL;

                        break;
                    }
                }
#if MVM_LOG_DEOPTS
                if (i == num_deopts)
                    fprintf(stderr, "JIT: can't find deopt all idx\n");
#endif
            }

            else {
                /* Not JITted; see if we can find the return address in the deopt table. */
                MVMint32 ret_offset = f->return_address - f->spesh_cand->bytecode;
                MVMint32 n = f->spesh_cand->num_deopts * 2;
                MVMint32 i;
                for (i = 0; i < n; i += 2) {
                    if (f->spesh_cand->deopts[i + 1] == ret_offset) {
                        /* Re-create any frames needed if we're in an inline; if not,
                        * just update return address. */
                        if (f->spesh_cand->inlines) {
                            MVMROOT(tc, f, {
                            MVMROOT(tc, l, {
                                uninline(tc, f, f->spesh_cand, ret_offset, f->spesh_cand->deopts[i], l);
                            });
                            });
                        }
                        else {
                            f->return_address = f->static_info->body.bytecode + f->spesh_cand->deopts[i];
                        }

                        /* No spesh cand/slots needed now. */
                        f->effective_spesh_slots = NULL;
                        f->spesh_cand            = NULL;

                        break;
                    }
                }
#if MVM_LOG_DEOPTS
                if (i == n)
                    fprintf(stderr, "Interpreter: can't find deopt all idx\n");
#endif
            }
        }
        l = f;
        f = f->caller;
    }
}
