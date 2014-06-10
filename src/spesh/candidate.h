/* A specialization candidate. */
struct MVMSpeshCandidate {
    /* The callsite we should have for a match. */
    MVMCallsite *cs;

    /* Guards on incoming args. */
    MVMSpeshGuard *guards;

    /* Number of guards we have. */
    MVMuint32 num_guards;

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

    /* Logging slots, used when we're in the log phase of producing
     * a specialization. */
    MVMCollectable **log_slots;

    /* Number of logging slots. */
    MVMuint32 num_log_slots;
    
    /* Function pointer to the JIT-ed function */
    MVMJitCode jitcode;

    /* Size of jit code (to be able to free it) */
    size_t jitcode_size;
};

/* The number of specializations we'll allow per static frame. */
#define MVM_SPESH_LIMIT 4

/* A specialization guard. */
struct MVMSpeshGuard {
    /* The kind of guard this is. */
    MVMint32 kind;

    /* The incoming argument slot it applies to. */
    MVMint32 slot;

    /* Object we might be wanting to match against. */
    MVMCollectable *match;
};

/* Kinds of guard we have. */
#define MVM_SPESH_GUARD_CONC    1   /* Value is concrete with match type. */
#define MVM_SPESH_GUARD_TYPE    2   /* Value is type object with match type. */
#define MVM_SPESH_GUARD_DC_CONC 3   /* Decont'd value is concrete with match type. */
#define MVM_SPESH_GUARD_DC_TYPE 4   /* Decont'd value is type object with match type. */

/* Functions for generating a specialization. */
MVMSpeshCandidate * MVM_spesh_candidate_setup(MVMThreadContext *tc,
    MVMStaticFrame *static_frame, MVMCallsite *callsite, MVMRegister *args);
void MVM_spesh_candidate_specialize(MVMThreadContext *tc, MVMStaticFrame *static_frame,
        MVMSpeshCandidate *candidate);
