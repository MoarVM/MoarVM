
struct MVMJitRegisterAllocator {
    MVM_DYNAR_DECL(MVMJitExprValue*, active);

    /* Values by node */
    MVMJitExprValue **values_by_node;

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

