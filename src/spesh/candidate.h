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
    MVMint32 num_spesh_slots;

    /* Deoptimization mappings. */
    MVMint32 *deopts;

    /* The number of deoptimization mappings we have. */
    MVMint32 num_deopts;
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
MVMSpeshCandidate * MVM_spesh_candidate_generate(MVMThreadContext *tc,
    MVMStaticFrame *static_frame, MVMCallsite *callsite, MVMRegister *args);
