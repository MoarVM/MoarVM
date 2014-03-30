/* The result produced from specializing bytecode. */
struct MVMSpeshCode {
    /* The specialized bytecode. */
    MVMuint8 *bytecode;

    /* Updated set of frame handlers. */
    MVMFrameHandler *handlers;
};

MVMSpeshCode * MVM_spesh_codegen(MVMThreadContext *tc, MVMSpeshGraph *g);
