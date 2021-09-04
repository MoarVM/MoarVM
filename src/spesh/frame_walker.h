/* State maintained during a spesh-aware frame walk. */
struct MVMSpeshFrameWalker {
    /* The current real MVMFrame that we are considering. */
    MVMFrame *cur_caller_frame;

    /* If we're currently looking through applicable inlines, the JIT position
     * or deopt offset to look for them based on, together with the current inline
     * we are looking at. */
    void *jit_position;
    MVMuint32 deopt_offset;
    MVMint32 inline_idx;

    /* If we're doing a walk of outer frames too, the current outer frame that
     * we are considering starting from the caller frame. */
    MVMFrame *cur_outer_frame;

    /* Should we walk the outer chain hanging off each dynamic frame? */
    MVMuint8 visit_outers;

    /* Should we visit the callers beyond the start frame (maybe after some
     * traversals before we start to iterate)? */
    MVMuint8 visit_callers;

    /* Did we start iterating yet? */
    MVMuint8 started;

    /* Did we just do a manual traversal, and so are on the frame we want to
     * start off with? */
    MVMuint8 traversed;

    /* Are we currently visiting the outer chain? */
    MVMuint8 visiting_outers;
};

/* Sentinel value to indicate there's no inline to explore. */
#define MVM_SPESH_FRAME_WALKER_NO_INLINE -2

void MVM_spesh_frame_walker_init(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMFrame *start,
        MVMuint8 visit_outers);
void MVM_spesh_frame_walker_init_for_outers(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
    MVMFrame *start);
MVMuint32 MVM_spesh_frame_walker_next(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMuint32 MVM_spesh_frame_walker_get_lex(MVMThreadContext *tc, MVMSpeshFrameWalker *fw,
        MVMString *name, MVMRegister **found_out, MVMuint16 *found_kind_out,
        MVMuint32 vivify, MVMFrame **found_frame);
MVMuint32 MVM_spesh_frame_walker_move_outer(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMuint32 MVM_spesh_frame_walker_move_caller(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMuint32 MVM_spesh_frame_walker_move_outer_skip_thunks(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw);
MVMuint32 MVM_spesh_frame_walker_move_caller_skip_thunks(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw);
MVMFrame * MVM_spesh_frame_walker_get_frame(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMObject * MVM_spesh_frame_walker_get_lexicals_hash(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMint64 MVM_spesh_frame_walker_get_lexical_primspec(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw, MVMString *name);
MVMObject * MVM_spesh_frame_walker_get_code(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);
MVMuint64 MVM_spesh_frame_walker_get_lexical_count(MVMThreadContext *tc, MVMSpeshFrameWalker *fw);

/* Gets the current frame we're walking. If we're in an inline, then it's the
 * frame holding the inline. */
MVM_STATIC_INLINE MVMFrame * MVM_spesh_frame_walker_current_frame(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw) {
    return MVM_UNLIKELY(fw->visiting_outers)
        ? fw->cur_outer_frame
        : fw->cur_caller_frame;
}

/* Returns non-zero if we're currently visiting an inline, zero otherwise. */
MVM_STATIC_INLINE MVMuint32 MVM_spesh_frame_walker_is_inline(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw) {
    return fw->inline_idx != MVM_SPESH_FRAME_WALKER_NO_INLINE;
}

/* Cleans up the spesh frame walker after use. */
MVM_STATIC_INLINE void MVM_spesh_frame_walker_cleanup(MVMThreadContext *tc,
        MVMSpeshFrameWalker *fw) {
    MVM_gc_root_temp_pop_n(tc, 2);
}
