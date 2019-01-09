/* The result produced from specializing bytecode. */
struct MVMSpeshCode {
    /* The specialized bytecode. */
    MVMuint8 *bytecode;

    /* The size of the produced bytecode. */
    MVMuint32 bytecode_size;

    /* Updated set of frame handlers. */
    MVMFrameHandler *handlers;

    /* Deopt usage info, which will be stored on the candidate. */
    MVMint32 *deopt_usage_info;
};

MVMSpeshCode * MVM_spesh_codegen(MVMThreadContext *tc, MVMSpeshGraph *g);
