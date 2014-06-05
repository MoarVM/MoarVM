/* Maximum size of bytecode we'll inline. */
#define MVM_SPESH_MAX_INLINE_SIZE 256

MVMSpeshGraph * MVM_spesh_inline_try_get_graph(MVMThreadContext *tc, MVMCode *target,
    MVMSpeshCandidate *cand);
