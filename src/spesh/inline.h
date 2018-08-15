/* Maximum size of bytecode we'll inline. */
#define MVM_SPESH_MAX_INLINE_SIZE 384

/* Inline table entry. The data is primarily used in deopt. */
struct MVMSpeshInline {
    /* Start and end position in the bytecode where we're inside of this
     * inline. */
    MVMuint32 start;
    MVMuint32 end;

    /* The static frame that was inlined. */
    MVMStaticFrame *sf;

    /* The register holding the inlined code ref. */
    MVMuint16 code_ref_reg;

    /* Start position of the locals and lexicals, so we can extract them
     * to the new frame. */
    MVMuint16 locals_start;
    MVMuint16 lexicals_start;

    /* The number of locals in the inline. */
    MVMuint16 num_locals;

    /* Result register and result type. */
    MVMuint16     res_reg;
    MVMReturnType res_type;

    /* Deopt index used to find return address. */
    MVMuint32 return_deopt_idx;

    /* If the inline became unreachable after being made, we'll mark it as
     * such, so we won't try and fix it up later. */
    MVMuint8 unreachable;

    /* Flag that we set if the inline has an instruction that may deopt. */
    MVMuint8 may_cause_deopt;

    /* Bit field of named args used to put in place during deopt, since we
     * typically don't update the array in specialized code. */
    MVMuint64 deopt_named_used_bit_field;
};

MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc,
    MVMSpeshGraph *inliner, MVMStaticFrame *target_sf, MVMSpeshCandidate *cand,
    MVMSpeshIns *invoke_ins, char **no_inline_reason);
MVMSpeshGraph * MVM_spesh_inline_try_get_graph_from_unspecialized(MVMThreadContext *tc,
    MVMSpeshGraph *inliner, MVMStaticFrame *target_sf, MVMSpeshIns *invoke_ins,
    MVMSpeshCallInfo *call_info, char **no_inline_reason);
void MVM_spesh_inline(MVMThreadContext *tc, MVMSpeshGraph *inliner,
    MVMSpeshCallInfo *call_info, MVMSpeshBB *invoke_bb,
    MVMSpeshIns *invoke, MVMSpeshGraph *inlinee, MVMStaticFrame *inlinee_sf,
    MVMSpeshOperand code_ref_reg, MVMuint32 proxy_deopt_idx);
