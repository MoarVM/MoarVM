/* Maximum size of bytecode we'll inline. */
#define MVM_SPESH_MAX_INLINE_SIZE 256

/* Inline table entry. The data is primarily used in deopt. */
struct MVMSpeshInline {
    /* Start and end position in the bytecode where we're inside of this
     * inline. */
    MVMuint32 start;
    MVMuint32 end;

    /* The static frame of the inlined code. */
    MVMStaticFrame *sf;

    /* Start position of the locals and lexicals, so we can extract them
     * to the new frame. */
    MVMuint16 locals_start;
    MVMuint16 lexicals_start;

    /* Result register and result type. */
    MVMuint16     res_reg;
    MVMReturnType res_type;
};

MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMCode *target,
    MVMSpeshCandidate *cand);
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
    MVMSpeshCallInfo *call_info, MVMSpeshBB *invoke_bb,
    MVMSpeshIns *invoke, MVMSpeshGraph *inlinee);
