#define MVM_PROGRAM_ENTRYPOINT_PROFILER_STATIC 0
#define MVM_PROGRAM_ENTRYPOINT_PROFILER_DYNAMIC 1
#define MVM_PROGRAM_ENTRYPOINT_SPESH 2
#define MVM_PROGRAM_ENTRYPOINT_JIT 3

#define MVM_PROGRAM_ENTRYPOINT_COUNT 4

struct MVMConfigurationProgramEntryPoint {
    MVMuint32 offset;
};

struct MVMConfigurationProgram {
    MVMuint8 *bytecode;

    MVMObject *string_heap;

    MVMuint8 *reg_types;
    MVMuint16 reg_count;

    MVMuint32 bytecode_length;

    MVMint16 entrypoints[MVM_PROGRAM_ENTRYPOINT_COUNT];
};

MVMint64 MVM_confprog_run(MVMThreadContext *tc, void *subject, MVMuint8 entrypoint, MVMint64 initial_feature_value);
void MVM_confprog_mark(MVMThreadContext *tc, MVMGCWorklist *worklist, MVMHeapSnapshotState *snapshot);
void MVM_confprog_install(MVMThreadContext *tc, MVMObject *bytecode, MVMObject *string_array, MVMObject *entrypoints);
