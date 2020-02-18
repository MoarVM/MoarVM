/* A specialization candidate. */
struct MVMSpeshCandidate {
    /* The callsite that this specialization is for. */
    MVMCallsite *cs;

    /* The type type that this specialization is for, or NULL if it's a
     * certian specialization. */
    MVMSpeshStatsType *type_tuple;

    /* Has the candidated been discarded? */
    MVMuint8 discarded;

    /* Length of the specialized bytecode in bytes. */
    MVMuint32 bytecode_size;

    /* The specialized bytecode. */
    MVMuint8 *bytecode;

    /* Frame handlers for this specialization. */
    MVMFrameHandler *handlers;

    /* Spesh slots, used to hold information for fast access. */
    MVMCollectable **spesh_slots;

    /* Number of spesh slots. */
    MVMuint32 num_spesh_slots;

    /* The number of deoptimization mappings we have. */
    MVMuint32 num_deopts;

    /* Deoptimization mappings. */
    MVMint32 *deopts;

    /* Bit field of named args used to put in place during deopt, since we
     * typically don't update the array in specialized code. */
    MVMuint64 deopt_named_used_bit_field;

    /* Deopt information produced by escape analysis and scalar replacement. */
    MVMSpeshPEADeopt deopt_pea;

    /* Number of inlines and inlines table; see graph.h for description of
     * the table format. */
    MVMuint32 num_inlines;
    MVMSpeshInline *inlines;

    /* The list of local types (only set up if we do inlines). */
    MVMuint16 *local_types;

    /* The list of lexical types (only set up if we do inlines). */
    MVMuint16 *lexical_types;

    /* Number of locals the specialized code has (may be different from the
     * original frame thanks to inlining). */
    MVMuint16 num_locals;

    /* Number of lexicals the specialized code has. */
    MVMuint16 num_lexicals;

    /* Memory sizes to allocate for work/env, taking into account inlining. */
    MVMuint32 work_size;
    MVMuint32 env_size;

    /* Number of handlers. */
    MVMuint32 num_handlers;

    /* JIT-code structure. */
    MVMJitCode *jitcode;

    /* Information used to reconstruct deoptimization usage info should we do
     * an inline of this candidate. It's stored as a sequence of integers of
     * the form:
     *  - Bytecode offset of writing instruction
     *  - Number of deopt indices that follow
     *  - The deopt indices
     *  There is a trailing -1 bytecode offset to mark the end of the data.
     */
    MVMint32 *deopt_usage_info;
};

/* Functions for creating and clearing up specializations. */
void MVM_spesh_candidate_add(MVMThreadContext *tc, MVMSpeshPlanned *p);
void MVM_spesh_candidate_destroy(MVMThreadContext *tc, MVMSpeshCandidate *candidate);
void MVM_spesh_candidate_discard_existing(MVMThreadContext *tc, MVMStaticFrame *sf);
