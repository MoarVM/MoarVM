/* The maximum size of bytecode we'll ever attempt to optimize. */
#define MVM_SPESH_MAX_BYTECODE_SIZE 65536

MVMuint32 MVM_spesh_threshold(MVMThreadContext *tc, MVMStaticFrame *sf);
