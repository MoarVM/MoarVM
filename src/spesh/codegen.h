/* The result produced from specializing bytecode. */
struct MVMSpeshCode {
    /* The specialized bytecode. */
    MVMuint8 *bytecode;

    /* The size of the produced bytecode. */
    MVMuint32 bytecode_size;

    /* Size of bytecode excluding instrumentation ops. */
    MVMuint32 normalized_bytecode_size;

    /* Updated set of frame handlers. */
    MVMFrameHandler *handlers;
};

MVMSpeshCode * MVM_spesh_codegen(MVMThreadContext *tc, MVMSpeshGraph *g);
