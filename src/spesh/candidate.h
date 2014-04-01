/* A specialization candidate. */
struct MVMSpeshCandidate {
    /* The callsite we should have for a match. */
    MVMCallsite *cs;

    /* The specialized bytecode. */
    MVMuint8 *bytecode;

    /* Length of the specialized bytecode in bytes. */
    MVMuint32 bytecode_size;

    /* Frame handlers for this specialization. */
    MVMFrameHandler *handlers;

    /* Spesh slots, used to hold information for fast access. */
    MVMCollectable **spesh_slots;

    /* Number of spesh slots. */
    MVMint32 num_spesh_slots;
};

/* The number of specializations we'll allow per static frame. */
#define MVM_SPESH_LIMIT 4

/* Functions for generating a specialization. */
MVMSpeshCandidate * MVM_spesh_candidate_generate(MVMThreadContext *tc,
    MVMStaticFrame *static_frame, MVMCallsite *callsite, MVMRegister *args);
