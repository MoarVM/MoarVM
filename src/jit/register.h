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

struct MVMJitRegisterAllocator {
    MVM_DYNAR_DECL(MVMJitValueDescriptor*, active);

    /* Values by node */
    MVMJitValueDescriptor **values_by_node;

    /* Register giveout ring */
    MVMint8 *free_reg;
    MVMuint8 *reg_use;

    MVMint32 reg_give, reg_take;

    /* topmost spill location used */
    MVMint32 spill_top;
    /* Bitmap of used registers */
    MVMint32 reg_lock;
};

void MVM_jit_register_allocate(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTileList *list);
