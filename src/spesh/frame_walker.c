#include "moar.h"

/* The spesh-aware frame walker allows walking through the dynamic chain,
 * optionally visiting its static chain at each point, and getting correct
 * results even if inlining has taken place.*/

/* Initializes the frame walker. The `MVMSpeshFrameWalker` object MUST be on
 * the system stack, and the cleanup function MUST be called after using it,
 * except in the case of an exception. This is because, since frames are GC
 * managed objects, it has to register the pointers to them with the GC, and
 * unregister them after the walk. Must call MVM_spesh_frame_walker_next after
 * this to be in a valid state to interrogate the first frame. */
void MVM_spesh_frame_walker_init(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame *start,
                                 MVMuint8 visit_outers) {
    fw->cur_caller_frame = start;
    fw->cur_outer_frame = NULL;
    fw->visit_outers = visit_outers;
    fw->started = 0;
    fw->visiting_outers = 0;
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(fw->cur_caller_frame));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&(fw->cur_outer_frame));
}

/* Sentinel value to indicate there's no inline to explore. */
#define NO_INLINE -2

/* Go to the next inline, if any. */
static void go_to_next_inline(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *f = fw->cur_caller_frame;
    MVMSpeshCandidate *cand = f->spesh_cand;
    MVMint32 i;
    if (fw->inline_idx == NO_INLINE)
        return;
    for (i = fw->inline_idx + 1; i < cand->num_inlines; i++) {
        if (fw->deopt_offset > cand->inlines[i].start && fw->deopt_offset <= cand->inlines[i].end) {
            /* Found an applicable inline. */
            fw->inline_idx = i;
            return;
        }
    }

    /* No inline available. */
    fw->inline_idx = NO_INLINE;
}

/* See if the current frame is specialized, and if so if we are in an inline.
 * If so, go to the innermost inline. */
static void go_to_first_inline(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVMFrame *f = fw->cur_caller_frame;
    if (f->spesh_cand && f->spesh_cand->inlines) {
        MVMint32 deopt_idx = MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc, f);
        if (deopt_idx >= 0) {
            fw->deopt_offset = f->spesh_cand->deopts[2 * deopt_idx + 1];
            fw->inline_idx = -1;
            go_to_next_inline(tc, fw);
            return;
        }
    }
    fw->inline_idx = NO_INLINE;
}

/* Moves to the next frame to visit. Returns non-zero if there was a next
 * frame to move to, and zero if there is not (and as such the iteration is
 * over). */
MVMuint32 MVM_spesh_frame_walker_next(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    if (!fw->started) {
        fw->started = 1;
        go_to_first_inline(tc, fw);
        return fw->cur_caller_frame ? 1 : 0;
    }
    else {
        MVMFrame *caller;

        /* If we are currently walking an outer chain, proceed along it,
         * and return the next frame along unless we reached the end. */
        if (fw->cur_outer_frame) {
            MVMFrame *outer = fw->cur_outer_frame->outer;
            if (outer) {
                fw->cur_outer_frame = outer;
                return 1;
            }
            else {
                /* End of the outer chain. Clear the currently visiting outer
                 * chain flag. */
                fw->visiting_outers = 0;
            }
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

        /* If we get here, we're looking for the next caller, if there is
         * one. Is there an inline to try and visit? If there is one, then
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
            fw->cur_caller_frame = caller;
            go_to_first_inline(tc, fw);
            return 1;
        }

        /* If we get here, there's nowhere further to go. */
        return 0;
    }
}

/* Gets the lexical in the current frame we're visiting, if it declares one.
 * Returns zero if there is no such lexical in that current frame. If there is
 * one, returns non-zero and populates found_out and found_kind_out. Will also
 * trigger vivification of the lexical if needed. */
MVMuint32 MVM_spesh_frame_walker_get_lex(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
                                         MVMString *name, MVMRegister **found_out,
                                         MVMuint16 *found_kind_out) {
    MVMFrame *cur_frame;
    MVMStaticFrame *sf;
    MVMuint32 base_index;
    MVMLexicalRegistry *lexical_names;
    if (fw->visiting_outers) {
        cur_frame = fw->cur_outer_frame;
        sf = cur_frame->static_info;
        base_index = 0;
    }
    else {
        cur_frame = fw->cur_caller_frame;
        if (fw->inline_idx == NO_INLINE) {
            sf = cur_frame->static_info;
            base_index = 0;
        }
        else {
            sf = cur_frame->spesh_cand->inlines[fw->inline_idx].sf;
            base_index = cur_frame->spesh_cand->inlines[fw->inline_idx].lexicals_start;
        }
    }
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
            if (kind == MVM_reg_obj && !result->o)
                MVM_frame_vivify_lexical(tc, cur_frame, index);
            return 1;
        }
    }
    return 0;
}

/* Cleans up the spesh frame walker after use. */
void MVM_spesh_frame_walker_cleanup(MVMThreadContext *tc, MVMSpeshFrameWalker *fw) {
    MVM_gc_root_temp_pop_n(tc, 2);
}
