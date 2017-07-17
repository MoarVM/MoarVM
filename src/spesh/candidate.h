/* A specialization candidate. */
struct MVMSpeshCandidate {
    /* The callsite we should have for a match. */
    MVMCallsite *cs;

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

    /* Atomic integer for the number of times we've entered the code so far
     * for the purpose of logging, in the trace phase. We used this as an
     * index into the log slots when running logging code. Once it hits the
     * limit on number of log attempts it increments no further. */
    AO_t log_enter_idx;

    /* Atomic integer for the number of times we need to exit the logging
     * version of the code. When this hits zero, we know we were the last
     * run, that there are no remaining runs, and so we should finalize
     * the specialization. */
    AO_t log_exits_remaining;

    /* The spesh graph, if we're still in the process of producing a
     * specialization for this candidate. NULL afterwards. */
    MVMSpeshGraph *sg;

    /* Number of inlines and inlines table; see graph.h for description of
     * the table format. */
    MVMint32 num_inlines;
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

    /* Whether this is a candidate we're in the process of doing OSR logging
     * on. */
    MVMuint32 osr_logging;

    /* JIT-code structure */
    MVMJitCode *jitcode;
};

/* The number of specializations we'll allow per static frame. */
#define MVM_SPESH_LIMIT 4

/* Functions for generating a specialization. */
MVMSpeshCandidate * MVM_spesh_candidate_setup(MVMThreadContext *tc,
    MVMStaticFrame *static_frame, MVMCallsite *callsite, MVMRegister *args,
    MVMint32 osr);
void MVM_spesh_candidate_specialize(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMSpeshCandidate *candidate);
void MVM_spesh_candidate_destroy(MVMThreadContext *tc, MVMSpeshCandidate *candidate);
