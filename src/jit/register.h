typedef enum {
    MVM_JIT_STORAGE_LOCAL,
    MVM_JIT_STORAGE_STACK,
    MVM_JIT_STORAGE_GPR,  /* general purpose register */
    MVM_JIT_STORAGE_FPR,  /* floating point register */
    MVM_JIT_STORAGE_NVR   /* non-volatile register */
}  MVMJitStorageClass;

struct MVMJitValueDescriptor {
    MVMJitTile *created_by;
    MVMint32 node;

    MVMJitStorageClass st_cls;
    MVMint16 st_pos;
    MVMint8  size;

    MVMint32 range_start, range_end;

    MVMJitValueDescriptor *next_by_node;
    MVMJitValueDescriptor *next_by_position;
};


void MVM_jit_register_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list);
