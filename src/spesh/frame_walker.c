#include "moar.h"

/* The spesh-aware frame walker allows walking through the dynamic chain,
 * optionally visiting its static chain at each point, and getting correct
 * results even if inlining has taken place.*/

/* Sentinel value to indicate there's no inline to explore. */
#define NO_INLINE -2

/* Initializes the frame walker. The `MVMSpeshFrameWalker` object MUST be on
 * the system stack, and the cleanup function MUST be called after using it,
 * except in the case of an exception. This is because, since frames are GC
 * managed objects, it has to register the pointers to them with the GC, and
 * unregister them after the walk. Must call MVM_spesh_frame_walker_next after
 * this to be in a valid state to interrogate the first frame. */
static void init_common(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame *start) {
    fw->cur_caller_frame = start;
    fw->cur_outer_frame = NULL;
    fw->started = fw->traversed = 0;
    fw->visiting_outers = 0;
    fw->inline_idx = NO_INLINE;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(fw->cur_caller_frame));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(fw->cur_outer_frame));
}
void MVM_spesh_frame_walker_init(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame *start,
                                 MVMuint8 visit_outers) {
    init_common(tc, fw, start);
    fw->visit_outers = visit_outers;
    fw->visit_callers = 1;
}

/* Initializes the frame walker for the case that we only want to iterate
 * the outer chain. */
void MVM_spesh_frame_walker_init_for_outers(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
                                            MVMFrame *start) {
    init_common(tc, fw, start);
    fw->visit_outers = 1;
    fw->visit_callers = 0;
}

/* Go to the next inline, if any. */
static void go_to_next_inline(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *f = fw->cur_caller_frame;
    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMJitCode *jitcode = f->spesh_cand->jitcode;
    if (fw->inline_idx == NO_INLINE)
        return;
    if (jitcode) {
        MVMint32 idx = MVM_jit_code_get_active_inlines(tc, jitcode, fw->jit_position, fw->inline_idx + 1);
        if (idx < jitcode->num_inlines) {
            fw->inline_idx = idx;
            return;
        }
    }
    else {
        MVMint32 i;
        for (i = fw->inline_idx + 1; i < cand->num_inlines; i++) {
            if (fw->deopt_offset > cand->inlines[i].start && fw->deopt_offset <= cand->inlines[i].end) {
                /* Found an applicable inline. */
                fw->inline_idx = i;
                return;
            }
        }
    }

    /* No inline available. */
    fw->inline_idx = NO_INLINE;
}

/* See if the current frame is specialized, and if so if we are in an inline.
 * If so, go to the innermost inline. */
static void go_to_first_inline(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame *prev) {
    MVMFrame *f = fw->cur_caller_frame;
    if (f->spesh_cand && f->spesh_cand->inlines) {
        MVMJitCode *jitcode = f->spesh_cand->jitcode;
        if (jitcode) {
            void *current_position = prev && MVM_frame_has_extra(prev) && prev->extra->caller_jit_position
                ? prev->extra->caller_jit_position
                : MVM_jit_code_get_current_position(tc, jitcode, f);
            MVMint32 idx = MVM_jit_code_get_active_inlines(tc, jitcode, current_position, 0);
            if (idx < jitcode->num_inlines) {
                fw->jit_position = current_position;
                fw->inline_idx = idx;
                return;
            }
        }
        else {
            MVMint32 deopt_idx = prev && MVM_frame_has_extra(prev) && prev->extra->caller_deopt_idx > 0
                ? prev->extra->caller_deopt_idx - 1
                : MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc, f);
            if (deopt_idx >= 0) {
                fw->deopt_offset = f->spesh_cand->deopts[2 * deopt_idx + 1];
                fw->inline_idx = -1;
                go_to_next_inline(tc, fw);
                return;
            }
        }
    }
    fw->inline_idx = NO_INLINE;
}

/* Moves one caller frame deeper, accounting for inlines. */
MVMuint32 move_one_caller(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *caller;

     /* Is there an inline to try and visit? If there is one, then
     * we will be placed on it. If there is not one, we will be placed
     * on the base frame containing inlines. Either way, we must have
     * something to go to. */
    if (fw->inline_idx != NO_INLINE) {
        go_to_next_inline(tc, fw);
        return 1;
    }

    /* Otherwise, really need to go out one frame, and then maybe go
     * to its first inline. */
    caller = fw->cur_caller_frame->caller;
    if (caller) {
        MVMFrame *prev = fw->cur_caller_frame;
        fw->cur_caller_frame = caller;
        go_to_first_inline(tc, fw, prev);
        return 1;
    }

    /* If we get here, there's nowhere further to go. */
    return 0;
}

/* Moves to the next frame to visit. Returns non-zero if there was a next
 * frame to move to, and zero if there is not (and as such the iteration is
 * over). */
MVMuint32 MVM_spesh_frame_walker_next(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    if (!fw->started) {
        fw->started = 1;
        go_to_first_inline(tc, fw, NULL);
        return fw->cur_caller_frame ? 1 : 0;
    }
    else if (fw->traversed) {
        /* Our last step was manually moving to the correct frame, so nothing
         * do be done. This was only set if the traversal reached a frame. */
        fw->traversed = 0;
        return 1;
    }
    else {
        /* If we are currently walking an outer chain, proceed along it,
         * and return the next frame along unless we reached the end. */
        if (fw->cur_outer_frame) {
            MVMFrame *outer = fw->cur_outer_frame->outer;
            fw->cur_outer_frame = outer;
            if (outer)
                return 1;
            else
                /* End of the outer chain. Clear the currently visiting outer
                 * chain flag. */
                fw->visiting_outers = 0;
        }

        /* Otherwise, we're currently visiting a caller, and it's time to try
         * visiting its outers if we're meant to do that. */
        else if (fw->visit_outers) {
            MVMFrame *outer;
            if (fw->inline_idx == NO_INLINE) {
                outer = fw->cur_caller_frame->outer;
            }
            else {
                MVMSpeshInline *i = &(fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx]);
                MVMCode *code = (MVMCode *)fw->cur_caller_frame->work[i->code_ref_reg].o;
                outer = code ? code->body.outer : NULL;
            }
            if (outer) {
                fw->cur_outer_frame = outer;
                fw->visiting_outers = 1;
                return 1;
            }
        }

        /* If we get here, we're looking for the next caller, provided we're
         * walking those. */
        return fw->visit_callers ? move_one_caller(tc, fw) : 0;
    }
}

/* Returns non-zero if we're currently visiting an inline, zero otherwise. */
MVMuint32 MVM_spesh_frame_walker_is_inline(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    return fw->inline_idx != NO_INLINE;
}

/* Gets the current frame we're walking. If we're in an inline, then it's the
 * frame holding the inline. */
MVMFrame * MVM_spesh_frame_walker_current_frame(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    return MVM_UNLIKELY(fw->visiting_outers)
        ? fw->cur_outer_frame
        : fw->cur_caller_frame;
}

static void find_lex_info(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame **cur_frame_out,
                          MVMStaticFrame **sf_out, MVMuint32 *base_index_out) {
    if (fw->visiting_outers) {
        *cur_frame_out = fw->cur_outer_frame;
        *sf_out = fw->cur_outer_frame->static_info;
        *base_index_out = 0;
    }
    else {
        *cur_frame_out = fw->cur_caller_frame;
        if (fw->inline_idx == NO_INLINE) {
            *sf_out = fw->cur_caller_frame->static_info;
            *base_index_out = 0;
        }
        else {
            *sf_out = fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx].sf;
            *base_index_out = fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx].lexicals_start;
        }
    }
}

/* Gets the lexical in the current frame we're visiting, if it declares one.
 * Returns zero if there is no such lexical in that current frame. If there is
 * one, returns non-zero and populates found_out and found_kind_out. Will also
 * trigger vivification of the lexical if needed. */
MVMuint32 MVM_spesh_frame_walker_get_lex(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
                                         MVMString *name, MVMRegister **found_out,
                                         MVMuint16 *found_kind_out, MVMuint32 vivify,
                                         MVMFrame **found_frame) {
    MVMFrame *cur_frame;
    MVMStaticFrame *sf;
    MVMuint32 base_index;
    MVMLexicalRegistry *lexical_names;
    find_lex_info(tc, fw, &cur_frame, &sf, &base_index);
    lexical_names = sf->body.lexical_names;
    if (lexical_names) {
        /* Indexes were formerly stored off-by-one to avoid semi-predicate issue. */
        MVMLexicalRegistry *entry;
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry) {
            MVMint32 index = base_index + entry->value;
            MVMRegister *result = &cur_frame->env[index];
            MVMuint16 kind = sf->body.lexical_types[entry->value];
            *found_out = result;
            *found_kind_out = kind;
            if (vivify && kind == MVM_reg_obj && !result->o) {
                MVMROOT(tc, cur_frame, {
                    MVM_frame_vivify_lexical(tc, cur_frame, index);
                });
            }
            if (found_frame)
                *found_frame = cur_frame;
            return 1;
        }
    }
    return 0;
}

/* Walk one outer frame. Valid before we start iterating. */
MVMuint32 MVM_spesh_frame_walker_move_outer(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *outer;
    if (fw->inline_idx == NO_INLINE) {
        outer = fw->cur_caller_frame->outer;
    }
    else {
        MVMSpeshInline *i = &(fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx]);
        MVMCode *code = (MVMCode *)fw->cur_caller_frame->work[i->code_ref_reg].o;
        outer = code ? code->body.outer : NULL;
    }
    fw->cur_caller_frame = outer;
    fw->cur_outer_frame = NULL;
    fw->inline_idx = NO_INLINE;
    fw->visiting_outers = 0;
    fw->started = 1;
    if (outer != NULL) {
        fw->traversed = 1;
        return 1;
    }
    else {
        return 0;
    }
}

/* Walk one caller frame. Valid before we start iterating. */
MVMuint32 MVM_spesh_frame_walker_move_caller(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    fw->started = 1;
    if (move_one_caller(tc, fw)) {
        fw->traversed = 1;
        return 1;
    }
    else {
        return 0;
    }
}

/* Walk one non-thunk outer frame. Valid before we start iterating. */
MVMuint32 MVM_spesh_frame_walker_move_outer_skip_thunks(MVMThreadContext *tc,
                                                        MVMSpeshFrameWalker *fw) {
    while (MVM_spesh_frame_walker_move_outer(tc, fw)) {
        if (!fw->cur_caller_frame->static_info->body.is_thunk)
            return 1;
    }
    return 0;
}

/* Walk one non-thunk caller frame. Valid before we start iterating. */
MVMuint32 MVM_spesh_frame_walker_move_caller_skip_thunks(MVMThreadContext *tc,
                                                         MVMSpeshFrameWalker *fw) {
    while (MVM_spesh_frame_walker_move_caller(tc, fw)) {
        MVMStaticFrame *sf = fw->inline_idx == NO_INLINE
            ? fw->cur_caller_frame->static_info
            : fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx].sf;
        if (!sf->body.is_thunk)
            return 1;
    }
    return 0;
}

/* If the frame walker is currently pointing to an exact frame, returns it.
 * If it's instead pointing at an inline, returns NULL. */
MVMFrame * MVM_spesh_frame_walker_get_frame(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    if (fw->visiting_outers)
        return fw->cur_outer_frame;
    if (fw->inline_idx == NO_INLINE)
        return fw->cur_caller_frame;
    return NULL;
}

/* Gets a hash of the lexicals at the current location. */
MVMObject * MVM_spesh_frame_walker_get_lexicals_hash(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *frame;
    MVMStaticFrame *sf;
    MVMuint32 base_index;
    MVMHLLConfig *hll = MVM_hll_current(tc);
    MVMObject *ctx_hash = MVM_repr_alloc_init(tc, hll->slurpy_hash_type);
    find_lex_info(tc, fw, &frame, &sf, &base_index);
    MVMROOT3(tc, ctx_hash, frame, sf, {
        MVMLexicalRegistry **lexreg = sf->body.lexical_names_list;
        MVMuint32 i;
        for (i = 0; i < sf->body.num_lexicals; i++) {
            MVMuint16 type = sf->body.lexical_types[i];
            MVMuint32 idx = base_index + lexreg[i]->value;
            switch (type) {
                case MVM_reg_obj: {
                    MVMObject *obj = frame->env[idx].o;
                    if (!obj)
                        obj = MVM_frame_vivify_lexical(tc, frame, idx);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, obj);
                    break;
                }
                case MVM_reg_str: {
                    MVMObject *bs = MVM_repr_box_str(tc, hll->str_box_type,
                        frame->env[idx].s);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bs);
                    break;
                }
                case MVM_reg_int8: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].i8);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_uint8: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].u8);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_int16: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].i16);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_uint16: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].u16);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_int32: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].i32);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_uint32: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].u32);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_int64: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].i64);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_uint64: {
                    MVMObject *bi = MVM_repr_box_int(tc, hll->int_box_type,
                        frame->env[idx].u64);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bi);
                    break;
                }
                case MVM_reg_num32: {
                    MVMObject *bn = MVM_repr_box_num(tc, hll->num_box_type,
                        frame->env[idx].n32);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bn);
                    break;
                }
                case MVM_reg_num64: {
                    MVMObject *bn = MVM_repr_box_num(tc, hll->num_box_type,
                        frame->env[idx].n64);
                    MVM_repr_bind_key_o(tc, ctx_hash, lexreg[i]->key, bn);
                    break;
                }
                default:
                    MVM_exception_throw_adhoc(tc,
                        "%s lexical type encountered when bulding context hash",
                            MVM_reg_get_debug_name(tc, type));
            }
        }
    });
    return ctx_hash;
}

/* Get the kind of lexical with the given name at the frame walker's current
 * location. Returns -1 if there is no such lexical. */
MVMint64 MVM_spesh_frame_walker_get_lexical_primspec(MVMThreadContext *tc,
                                                     MVMSpeshFrameWalker *fw, MVMString *name) {
    MVMFrame *cur_frame;
    MVMStaticFrame *sf;
    MVMuint32 base_index;
    MVMLexicalRegistry *lexical_names;
    find_lex_info(tc, fw, &cur_frame, &sf, &base_index);
    lexical_names = sf->body.lexical_names;
    if (lexical_names) {
        MVMLexicalRegistry *entry;
        MVM_HASH_GET(tc, lexical_names, name, entry)
        if (entry)
            return MVM_frame_translate_to_primspec(tc, sf->body.lexical_types[entry->value]);
    }
    return -1;
}

/* Gets the code ref at the frame walker's current location. */
MVMObject * MVM_spesh_frame_walker_get_code(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    if (fw->visiting_outers)
        return fw->cur_outer_frame->code_ref;
    if (fw->inline_idx == NO_INLINE)
        return fw->cur_caller_frame->code_ref;
    return fw->cur_caller_frame->work[
        fw->cur_caller_frame->spesh_cand->inlines[fw->inline_idx].code_ref_reg
    ].o;
}

/* Gets a count of the number of lexicals in the frame walker's current
 * location. */
MVMuint64 MVM_spesh_frame_walker_get_lexical_count(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *cur_frame;
    MVMStaticFrame *sf;
    MVMuint32 base_index;
    MVMLexicalRegistry *lexical_names;
    find_lex_info(tc, fw, &cur_frame, &sf, &base_index);
    lexical_names = sf->body.lexical_names;
    return lexical_names ? (MVMuint64)HASH_CNT(hash_handle, lexical_names) : 0;
}

/* Cleans up the spesh frame walker after use. */
void MVM_spesh_frame_walker_cleanup(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVM_gc_root_temp_pop_n(tc, 2);
}
