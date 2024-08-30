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
 * non-inline, bit of the code - in which case we've nothing to do once we
 * have determined that.
 *
 * We can rely on the frame we are doing uninling on always being the top
 * record on the callstack.
 */
static void uninline(MVMThreadContext *tc, MVMFrame *f, MVMSpeshCandidate *cand,
                     MVMuint32 offset, MVMint32 all, MVMint32 is_pre) {
    /* Make absolutely sure this is the top thing on the callstack. */
    assert(MVM_callstack_current_frame(tc) == f);

    /* We know that nothing can reference an inlined frame (trivially - it did
     * not exist to reference!) Thus it can be created on the stack. We need to
     * recreate the frames deepest first. The inlines list is sorted most nested
     * first, thus we traverse it in the opposite order. */
    MVMint32 i;
    for (i = cand->body.num_inlines - 1; i >= 0; i--) {
        MVMuint32 start = cand->body.inlines[i].start;
        MVMuint32 end = cand->body.inlines[i].end;
        if ((is_pre ? offset >= start : offset > start) &&
                (all || !is_pre ? offset <= end : offset < end)) {
            /* Grab the current frame, which is the caller of this inline. */
            MVMFrame *caller = MVM_callstack_current_frame(tc);

            /* Does the inline have any dispatch resume arg initializations?
             * If so, we'll need to recreate a dispatch run record under the
             * call frame, in order that we can successfully resume. */
            if (cand->body.inlines[i].first_spesh_resume_init != -1) {
                MVMint32 j = cand->body.inlines[i].last_spesh_resume_init;
                while (j >= cand->body.inlines[i].first_spesh_resume_init) {
                    /* Allocate the resume init record. */
                    MVMSpeshResumeInit *ri = &(cand->body.resume_inits[j]);
                    MVMCallStackDeoptedResumeInit *dri =
                        MVM_callstack_allocate_deopted_resume_init(tc, ri);

                    /* Evacuate the current dispatch state. */
                    dri->state = f->work[ri->state_register].o;

                    /* Evacuate all non-constant resume init args. */
                    if (dri->dpr->init_values) {
                        /* Complex init values; make sure only to copy args
                         * and temporaries. */
                        MVMuint16 k;
                        for (k = 0; k < dri->dpr->init_callsite->flag_count; k++) {
                            switch (dri->dpr->init_values[k].source) {
                                case MVM_DISP_RESUME_INIT_ARG:
                                case MVM_DISP_RESUME_INIT_TEMP:
                                    dri->args[k] = f->work[ri->init_registers[k]];
                                    break;
                                default:
                                    /* Constant, ignore. */
                                    break;
                            }
                        }
                    }
                    else {
                        /* Just the plain args, so we find them linearly in. */
                        MVMuint16 k;
                        for (k = 0; k < dri->dpr->init_callsite->flag_count; k++)
                            dri->args[k] = f->work[ri->init_registers[k]];
                    }

                    j--;
                }
            }

            /* Resolve the inline's code object and static frame. */
            MVMStaticFrame *usf = cand->body.inlines[i].sf;
            MVMCode *ucode = (MVMCode *)f->work[cand->body.inlines[i].code_ref_reg].o;
            if (REPR(ucode)->ID != MVM_REPR_ID_MVMCode)
                MVM_panic(1, "Deopt: did not find code object when uninlining");

            /* Make a record for it on the stack; the MVMFrame is contained in
             * it. Set up the frame. Note that this moves tc->stack_top, so we
             * are now considered to be in this frame. */
            MVMCallStackFrame *urecord = MVM_callstack_allocate_frame(tc,
                    usf->body.work_size, usf->body.env_size);
            MVMFrame *uf = &(urecord->frame);
            MVM_frame_setup_deopt(tc, uf, usf, ucode);
            uf->caller = caller;
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

            /* Update our caller's return info. */
            caller->return_type = cand->body.inlines[i].res_type;
            caller->return_value = caller->return_type == MVM_RETURN_VOID
                ? NULL
                : caller->work + cand->body.inlines[i].res_reg;
            caller->return_address = caller->static_info->body.bytecode +
                cand->body.deopts[2 * cand->body.inlines[i].return_deopt_idx];

            /* Store the callsite, in case we need it for further processing
             * of arguments. Do enough to make sure we've got clean enough
             * state in the param processing context */
            uf->params.arg_info.callsite = cand->body.inlines[i].cs;
            uf->params.arg_info.map = (MVMuint16*)(caller->return_address
                - cand->body.inlines[i].cs->flag_count * 2);
            uf->params.arg_info.source = caller->work;
            uf->params.named_used_size = MVM_callsite_num_nameds(tc, cand->body.inlines[i].cs);

            /* Store the named argument used bit field, since if we deopt in
             * argument handling code we may have missed some. */
            if (cand->body.inlines[i].deopt_named_used_bit_field)
                uf->params.named_used.bit_field = cand->body.inlines[i].deopt_named_used_bit_field;
        }
    }

    /* By this point, either we did some inlining and the deepest uninlined
     * frame is on the top of the callstack, or there was nothing to uninline
     * and so we're just in the same frame. Since the deopt target when we
     * are in an inline relates to the deepest frame, we can just leave our
     * caller to take the appropriate action to move either the interpreter
     * or return address to the deopt'd one. */
}

/* We optimize away some bits of args checking; here we re-instate the used named
 * arguments bit field, which is required in unoptimized code. */
static void deopt_named_args_used(MVMThreadContext *tc, MVMFrame *f) {
    if (f->spesh_cand->body.deopt_named_used_bit_field)
        f->params.named_used.bit_field = f->spesh_cand->body.deopt_named_used_bit_field;
}

/* Materialize an individual replaced object. */
static void materialize_object(MVMThreadContext *tc, MVMFrame *f, MVMuint16 **materialized,
                               MVMuint16 info_idx, MVMuint16 target_reg) {
    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMObject *obj;

    if (!*materialized)
        *materialized = MVM_calloc(MVM_VECTOR_ELEMS(cand->body.deopt_pea.materialize_info), sizeof(MVMuint16));

    if ((*materialized)[info_idx]) {
        /* Register indexes are offset by 1, so 0 can mean "uninitialized" */
        obj = f->work[(*materialized)[info_idx] - 1].o;
    }
    else {
        MVMSpeshPEAMaterializeInfo *mi = &(cand->body.deopt_pea.materialize_info[info_idx]);
        MVMSTable *st = (MVMSTable *)cand->body.spesh_slots[mi->stable_sslot];
        MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
        MVMROOT2(tc, f, cand) {
            obj = MVM_gc_allocate_object(tc, st);

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
            /* Store register index offset by 1, so 0 can indicate "uninitialized" */
            (*materialized)[info_idx] = target_reg + 1;
        }
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Materialized a %s\n", st->debug_name);
#endif
    }

    f->work[target_reg].o = obj;
}

/* Materialize all replaced objects that need to be at this deopt index. */
static void materialize_replaced_objects(MVMThreadContext *tc, MVMFrame *f, MVMint32 deopt_index) {
    MVMuint32 i;
    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMuint32 num_deopt_points = MVM_VECTOR_ELEMS(cand->body.deopt_pea.deopt_point);
    MVMuint16 *materialized = NULL;
    MVMROOT2(tc, f, cand) {
        for (i = 0; i < num_deopt_points; i++) {
            MVMSpeshPEADeoptPoint *dp = &(cand->body.deopt_pea.deopt_point[i]);
            if (dp->deopt_point_idx == deopt_index)
                materialize_object(tc, f, &materialized, dp->materialize_info_idx, dp->target_reg);
        }
    }
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
    materialize_replaced_objects(tc, f, deopt_idx);
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
        MVMuint32 deopt_offset = MVM_spesh_deopt_bytecode_pos(f->spesh_cand->body.deopts[deopt_idx * 2 + 1]);
        MVMint32 is_pre = MVM_spesh_deopt_is_pre(f->spesh_cand->body.deopts[deopt_idx * 2 + 1]);
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Will deopt %u -> %u\n", deopt_offset, deopt_target);
#endif
        MVMFrame *top_frame;
        MVMROOT(tc, f) {
            begin_frame_deopt(tc, f, deopt_idx);

            /* Perform any uninlining. */
            if (f->spesh_cand->body.inlines) {
                /* Perform uninlining. The top frame may have changes, so sync things
                 * up. */
                uninline(tc, f, f->spesh_cand, deopt_offset, 0, is_pre);
                top_frame = MVM_callstack_current_frame(tc);
                tc->cur_frame = top_frame;
                *(tc->interp_reg_base) = top_frame->work;
                *(tc->interp_cu) = top_frame->static_info->body.cu;
            }
            else {
                /* No uninlining, so we know the top frame didn't change. */
                top_frame = f;
            }
        }

        /* Move the program counter of the interpreter. */
        *(tc->interp_cur_op)         = top_frame->static_info->body.bytecode + deopt_target;
        *(tc->interp_bytecode_start) = top_frame->static_info->body.bytecode;
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Completed deopt_one in '%s' (cuid '%s')\n",
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.name),
            MVM_string_utf8_encode_C_string(tc, tc->cur_frame->static_info->body.cuuid));
#endif
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
MVMint32 MVM_spesh_deopt_find_inactive_frame_deopt_idx(MVMThreadContext *tc,
    MVMFrame *f, MVMSpeshCandidate *spesh_cand)
{
    /* Is it JITted code? */
    if (spesh_cand->body.jitcode) {
        MVMJitCode *jitcode = spesh_cand->body.jitcode;
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
        MVMuint32 ret_offset = (f == tc->cur_frame ? *(tc->interp_cur_op) : f->return_address) - spesh_cand->body.bytecode;
        MVMint32 n = spesh_cand->body.num_deopts * 2;
        MVMint32 i;
        for (i = 0; i < n; i += 2) {
            if (MVM_spesh_deopt_bytecode_pos(spesh_cand->body.deopts[i + 1]) == ret_offset) {
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
    MVMSpeshCandidate *spesh_cand = frame->spesh_cand;
#if MVM_LOG_DEOPTS
    fprintf(stderr, "Lazy deopt on unwind of frame '%s' (cuid '%s')\n",
        MVM_string_utf8_encode_C_string(tc, frame->static_info->body.name),
        MVM_string_utf8_encode_C_string(tc, frame->static_info->body.cuuid));
#endif

    /* Find the deopt index, and assuming it's found, deopt. */
    MVMint32 deopt_idx = MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc, frame, spesh_cand);
    if (deopt_idx >= 0) {
        MVMuint32 deopt_target = spesh_cand->body.deopts[deopt_idx * 2];
        MVMuint32 deopt_offset = MVM_spesh_deopt_bytecode_pos(spesh_cand->body.deopts[deopt_idx * 2 + 1]);

        MVMFrame *top_frame;
        MVMROOT(tc, frame) {
            begin_frame_deopt(tc, frame, deopt_idx);

            /* Potentially need to uninline. This leaves the top frame being the
             * one we're returning into. Otherwise, the top frame is the current
             * one. */
            if (spesh_cand->body.inlines) {
                uninline(tc, frame, spesh_cand, deopt_offset, 1, 0);
                top_frame = MVM_callstack_current_frame(tc);
            }
            else {
                top_frame = frame;
            }
        }

        /* Rewrite return address in the current top frame and sync current
         * frame. */
        top_frame->return_address = top_frame->static_info->body.bytecode + deopt_target;
#if MVM_LOG_DEOPTS
        fprintf(stderr, "    Deopt %u -> %u without uninlining\n", deopt_offset, deopt_target);
#endif
        tc->cur_frame = top_frame;
        finish_frame_deopt(tc, frame);
    }

    /* Sync interpreter so it's ready to continue running this deoptimized
     * frame. */
    *(tc->interp_cur_op) = tc->cur_frame->return_address;
    *(tc->interp_bytecode_start) = MVM_frame_effective_bytecode(tc->cur_frame);
    *(tc->interp_reg_base) = tc->cur_frame->work;
    *(tc->interp_cu) = tc->cur_frame->static_info->body.cu;

    /* Update the record to indicate we're no long in need of deopt. */
    record->kind = record->orig_kind;
}
