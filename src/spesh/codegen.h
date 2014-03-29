/* The result produced from specializing bytecode. */
struct MVMSpeshCode {
    /* The specialized bytecode. */
    MVMuint8 *bytecode;
};

MVMSpeshCode * MVM_spesh_codegen(MVMThreadContext *tc, MVMSpeshGraph *g);
