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
                     MVMuint32 offset, MVMuint32 deopt_offset, MVMFrame *callee) {
    MVMFrame      *last_uninlined = NULL;
    MVMuint16      last_res_reg = 0;
    MVMReturnType  last_res_type = 0;
    MVMuint32      last_return_deopt_idx = 0;
    MVMuint32 i;
    for (i = 0; i < cand->body.num_inlines; i++) {
        if (offset > cand->body.inlines[i].start && offset <= cand->body.inlines[i].end) {
            /* Create the frame. */
            MVMCode        *ucode = (MVMCode *)f->work[cand->body.inlines[i].code_ref_reg].o;
            MVMStaticFrame *usf   = cand->body.inlines[i].sf;
            MVMFrame       *uf;
            if (REPR(ucode)->ID != MVM_REPR_ID_MVMCode)
                MVM_panic(1, "Deopt: did not find code object when uninlining");
            MVMROOT5(tc, f, callee, last_uninlined, usf, cand, {
                uf = MVM_frame_create_for_deopt(tc, usf, ucode);
            });
#if MVM_LOG_DEOPTS
            fprintf(stderr, "    Recreated frame '%s' (cuid '%s')\n",
                MVM_string_utf8_encode_C_string(tc, usf->body.name),
                MVM_string_utf8_encode_C_string(tc, usf->body.cuuid));
#endif

            /* Copy the locals and lexicals into place. */
            if (usf->body.num_locals)
                memcpy(uf->work, f->work + cand->body.inlines[i].locals_start,
                    usf->body.num_locals * sizeof(MVMRegister));
            if (usf->body.num_lexicals)
                memcpy(uf->env, f->env + cand->body.inlines[i].lexicals_start,
                    usf->body.num_lexicals * sizeof(MVMRegister));

            /* Store the callsite, in case we need it for further processing
             * of arguments. (TODO may need to consider the rest of the arg
             * processing context too.) */
            uf->params.version = MVM_ARGS_LEGACY;
            uf->params.legacy.callsite = cand->body.inlines[i].cs;

            /* Store the named argument used bit field, since if we deopt in
             * argument handling code we may have missed some. */
            if (cand->body.inlines[i].deopt_named_used_bit_field)
                uf->params.named_used.bit_field = cand->body.inlines[i].deopt_named_used_bit_field;

            /* Did we already uninline a frame? */
            if (last_uninlined) {
                /* Yes; multi-level un-inline. Switch it back to deopt'd
                 * code. */
                uf->effective_spesh_slots = NULL;
                uf->spesh_cand            = NULL;

                /* Set up the return location. */
                uf->return_address = usf->body.bytecode +
                    cand->body.deopts[2 * last_return_deopt_idx];

                /* Set result type and register. */
                uf->return_type = last_res_type;
                if (last_res_type == MVM_RETURN_VOID)
                    uf->return_value = NULL;
                else
                    uf->return_value = uf->work + last_res_reg;

                /* Set up last uninlined's caller to us. */
                MVM_ASSERT_NOT_FROMSPACE(tc, uf);
                MVM_ASSIGN_REF(tc, &(last_uninlined->header), last_uninlined->caller, uf);
            }
            else {
                /* First uninlined frame. Are we in the middle of the call
                 * stack (and thus in deopt_all)? */
                if (callee) {
                    /* Tweak the callee's caller to the uninlined frame, not
                     * the frame holding the inlinings. */
                    MVM_ASSERT_NOT_FROMSPACE(tc, uf);
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
                        MVMuint16 ret_reg  = orig_reg - cand->body.inlines[i].locals_start;
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

            /* Update tracking variables for last uninline. */
            last_uninlined        = uf;
            last_res_reg          = cand->body.inlines[i].res_reg;
            last_res_type         = cand->body.inlines[i].res_type;
            last_return_deopt_idx = cand->body.inlines[i].return_deopt_idx;
        }
    }
    if (last_uninlined) {
        /* Set return address, which we need to resolve to the deopt'd one. */
        f->return_address = f->static_info->body.bytecode +
            cand->body.deopts[2 * last_return_deopt_idx];

        /* Set result type and register. */
        f->return_type = last_res_type;
        if (last_res_type == MVM_RETURN_VOID)
            f->return_value = NULL;
        else
            f->return_value = f->work + last_res_reg;

        /* Set up inliner as the caller, given we now have a direct inline. */
        MVM_ASSERT_NOT_FROMSPACE(tc, f);
        MVM_ASSIGN_REF(tc, &(last_uninlined->header), last_uninlined->caller, f);

        /* Clear our current callsite. Inlining does not set this up, so it may
         * well be completely bogus with regards to the current call. */
        f->cur_args_callsite = NULL;
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
    if (f->spesh_cand->body.deopt_named_used_bit_field)
        f->params.named_used.bit_field = f->spesh_cand->body.deopt_named_used_bit_field;
}

/* Materialize an individual replaced object. */
static void materialize_object(MVMThreadContext *tc, MVMFrame *f, MVMObject ***materialized,
                               MVMuint16 info_idx, MVMuint16 target_reg) {
    MVMSpeshCandidate *cand = f->spesh_cand;
    if (!*materialized)
        *materialized = MVM_calloc(MVM_VECTOR_ELEMS(cand->body.deopt_pea.materialize_info), sizeof(MVMObject *));
    if (!(*materialized)[info_idx]) {
        MVMSpeshPEAMaterializeInfo *mi = &(cand->body.deopt_pea.materialize_info[info_idx]);
        MVMSTable *st = (MVMSTable *)cand->body.spesh_slots[mi->stable_sslot];
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
        MVMROOT2(tc, f, cand, {
            MVMObject *obj = MVM_gc_allocate_object(tc, st);
            char *data = (char *)OBJECT_BODY(obj);
            MVMuint32 num_attrs = repr_data->num_attributes;
            MVMuint32 i;
            for (i = 0; i < num_attrs; i++) {
                MVMRegister value = f->work[mi->attr_regs[i]];
                MVMuint16 offset = repr_data->attribute_offsets[i];
                MVMSTable *flattened = repr_data->flattened_stables[i];
                if (flattened) {
                    const MVMStorageSpec *ss = flattened->REPR->get_storage_spec(tc, flattened);
                    switch (ss->boxed_primitive) {
                        case MVM_STORAGE_SPEC_BP_INT:
                            flattened->REPR->box_funcs.set_int(tc, flattened, obj,
                                (char *)data + offset, value.i64);
                            break;
                        case MVM_STORAGE_SPEC_BP_NUM:
                            flattened->REPR->box_funcs.set_num(tc, flattened, obj,
                                (char *)data + offset, value.n64);
                            break;
                        case MVM_STORAGE_SPEC_BP_STR:
                            flattened->REPR->box_funcs.set_str(tc, flattened, obj,
                                (char *)data + offset, value.s);
                            break;
                        default:
                            MVM_panic(1, "Unimplemented case of native attribute deopt materialization");
                    }
                }
                else {
                    *((MVMObject **)(data + offset)) = value.o;
                }
            }
            (*materialized)[info_idx] = obj;
        });
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Materialized a %s\n", st->debug_name);
#endif
    }
    f->work[target_reg].o = (*materialized)[info_idx];
}

/* Materialize all replaced objects that need to be at this deopt index. */
static void materialize_replaced_objects(MVMThreadContext *tc, MVMFrame *f, MVMint32 deopt_index) {
    MVMuint32 i;
    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMuint32 num_deopt_points = MVM_VECTOR_ELEMS(cand->body.deopt_pea.deopt_point);
    MVMObject **materialized = NULL;
    MVMROOT2(tc, f, cand, {
        for (i = 0; i < num_deopt_points; i++) {
            MVMSpeshPEADeoptPoint *dp = &(cand->body.deopt_pea.deopt_point[i]);
            if (dp->deopt_point_idx == deopt_index)
                materialize_object(tc, f, &materialized, dp->materialize_info_idx, dp->target_reg);
        }
    });
    MVM_free(materialized);
}

/* Perform actions common to the deopt of a frame before we do any kind of
 * address rewriting, whether eager or lazy. */
static void begin_frame_deopt(MVMThreadContext *tc, MVMFrame *f, MVMuint32 deopt_idx) {
    deopt_named_args_used(tc, f);
    clear_dynlex_cache(tc, f);

    /* Materialize any replaced objects first, then if we have stuff replaced
     * in inlines then uninlining will take care of moving it out into the
     * frames where it belongs. */
    MVMROOT(tc, f, {
        materialize_replaced_objects(tc, f, deopt_idx);
    });
}

/* Perform actions common to the deopt of a frame after we do any kind of
 * address rewriting, whether eager or lazy. */
static void finish_frame_deopt(MVMThreadContext *tc, MVMFrame *f) {
    f->effective_spesh_slots = NULL;
    f->spesh_cand = NULL;
    f->jit_entry_label = NULL;
}

/* De-optimizes the currently executing frame, provided it is specialized and
 * at a valid de-optimization point. Typically used when a guard fails. */
void MVM_spesh_deopt_one(MVMThreadContext *tc, MVMuint32 deopt_idx) {
    MVMFrame *f = tc->cur_frame;
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_one(tc);
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Deopt one requested by interpreter in frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    assert(f->spesh_cand != NULL);
    assert(deopt_idx < f->spesh_cand->body.num_deopts);
    if (f->spesh_cand) {
        MVMuint32 deopt_target = f->spesh_cand->body.deopts[deopt_idx * 2];
        MVMuint32 deopt_offset = f->spesh_cand->body.deopts[deopt_idx * 2 + 1];
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Will deopt %u -> %u\n", deopt_offset, deopt_target);
#endif
        begin_frame_deopt(tc, f, deopt_idx);

        /* Check if we have inlines. */
        if (f->spesh_cand->body.inlines) {
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
#if MVM_LOG_DEOPTS
            fprintf(stderr, "    Completed deopt_one in '%s' (cuid '%s') with potential uninlining\n",
                MVM_string_utf8_encode_C_string(tc, f->static_info->body.name),
                MVM_string_utf8_encode_C_string(tc, f->static_info->body.cuuid));
#endif
        }
        else {
            /* No inlining; simple case. Switch back to the original code. */
            *(tc->interp_cur_op)         = f->static_info->body.bytecode + deopt_target;
            *(tc->interp_bytecode_start) = f->static_info->body.bytecode;
#if MVM_LOG_DEOPTS
            fprintf(stderr, "    Completed deopt_one in '%s' (cuid '%s')\n",
                MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
                MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
        }
        finish_frame_deopt(tc, f);
    }
    else {
        MVM_oops(tc, "deopt_one failed for %s (%s)",
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
    }

    MVM_CHECK_CALLER_CHAIN(tc, tc->cur_frame);
}

/* Walk the call stack, excluding the current frame, looking for specialized
 * call frames. If we find them, mark them as needing to be lazily deopt'd
 * when unwind reaches them. (This allows us to only ever deopt the stack
 * top.) */
void MVM_spesh_deopt_all(MVMThreadContext *tc) {
    /* Logging/profiling for global deopt. */
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Deopt all requested in frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
    if (tc->instance->profiling)
        MVM_profiler_log_deopt_all(tc);

    /* Create iterator and skip a frame. */
    MVMCallStackIterator iter;
    MVM_callstack_iter_frame_init(tc, &iter, tc->stack_top);
    if (!MVM_callstack_iter_move_next(tc, &iter))
        return;

    /* Go throught the frames looking for specialized, non-deopt, ones. */
    while (MVM_callstack_iter_move_next(tc, &iter)) {
        MVMCallStackRecord *record = MVM_callstack_iter_current(tc, &iter);
        if (record->kind != MVM_CALLSTACK_RECORD_DEOPT_FRAME) {
            MVMFrame *frame = MVM_callstack_record_to_frame(record);
            if (frame->spesh_cand) {
                /* Needs deoptimizing; mark it as such. */
                record->orig_kind = record->kind;
                record->kind = MVM_CALLSTACK_RECORD_DEOPT_FRAME;
#if MVM_LOG_DEOPTS
                fprintf(stderr, "  Marked frame '%s' (cuid '%s') for lazy deopt\n",
                    MVM_string_utf8_encode_C_string(tc, frame->static_info->body.name),
                    MVM_string_utf8_encode_C_string(tc, frame->static_info->body.cuuid));
#endif
            }
        }
    }
}

/* Takes a frame that we're lazily deoptimizing and finds the currently
 * active deopt index at the point of the call it was making. Returns -1 if
 * none can be resolved. */
MVMint32 MVM_spesh_deopt_find_inactive_frame_deopt_idx(MVMThreadContext *tc, MVMFrame *f) {
    /* Is it JITted code? */
    if (f->spesh_cand->body.jitcode) {
        MVMJitCode *jitcode = f->spesh_cand->body.jitcode;
        MVMuint32 idx = MVM_jit_code_get_active_deopt_idx(tc, jitcode, f);
        if (idx < jitcode->num_deopts) {
            MVMint32 deopt_idx = jitcode->deopts[idx].idx;
#if MVM_LOG_DEOPTS
            fprintf(stderr, "    Found deopt label for JIT (idx %d)\n", deopt_idx);
#endif
            return deopt_idx;
        }
    }
    else {
        /* Not JITted; see if we can find the return address in the deopt table. */
        MVMint32 ret_offset = f->return_address - f->spesh_cand->body.bytecode;
        MVMint32 n = f->spesh_cand->body.num_deopts * 2;
        MVMint32 i;
        for (i = 0; i < n; i += 2) {
            if (f->spesh_cand->body.deopts[i + 1] == ret_offset) {
                MVMint32 deopt_idx = i / 2;
#if MVM_LOG_DEOPTS
                fprintf(stderr, "    Found deopt index for interpeter (idx %d)\n", deopt_idx);
#endif
                return deopt_idx;
            }
        }
    }
#if MVM_LOG_DEOPTS
    fprintf(stderr, "    Can't find deopt all idx\n");
#endif
    return -1;
}

/* Called during stack unwinding when we reach a frame that was marked as
 * needing to be deoptimized lazily. Performs that deoptimization. Note
 * that this may actually modify the call stack by adding new records on
 * top of it, if we have to uninline. */
void MVM_spesh_deopt_during_unwind(MVMThreadContext *tc) {
    /* Get the frame from the record. If we're calling this, we know it's the
     * stack top one. */
    MVMCallStackRecord *record = tc->stack_top;
    MVMFrame *frame = MVM_callstack_record_to_frame(record);
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Lazy deopt on unwind of frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, frame->static_info->body.cuuid));
#endif

    /* Find the deopt index, and assuming it's found, deopt. */
    MVMint32 deopt_idx = MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc, frame);
    if (deopt_idx >= 0) {
        begin_frame_deopt(tc, frame, deopt_idx);

        /* Rewrite return address. */
        MVMuint32 deopt_target = frame->spesh_cand->body.deopts[deopt_idx * 2];
        frame->return_address = frame->static_info->body.bytecode + deopt_target;

        finish_frame_deopt(tc, frame);
    }

    /* Update the record to indicate we're no long in need of deopt. */
    record->kind = record->orig_kind;
}
