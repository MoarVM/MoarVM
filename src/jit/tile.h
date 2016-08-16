struct MVMJitTileTemplate {
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTile *tile, MVMJitExprTree *tree);
    const MVMint8 *path;
    const char    *expr;
    MVMint32  left_sym;
    MVMint32 right_sym;

    MVMint32  num_values;
    MVMint32  value_bitmap;
    MVMJitExprVtype vtype;
};

struct MVMJitTile {
    const MVMJitTileTemplate *template;
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitTile *tile, MVMJitExprTree *tree);
    MVMint32 node;
    MVMint32 num_values;
    /* buffers for the args of this (pseudo) tile */
    MVMJitValue *values[8];
    MVMJitExprNode args[8];
};

struct MVMJitTileList {
    MVMJitExprTree *tree;
    MVM_VECTOR_DECL(MVMJitTile*, items);
    /* TODO implement structures to mark basic blocks */
};

MVMJitTile     * MVM_jit_tile_make(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                   void *emit, MVMint32 node, MVMint32 nargs, ...);
MVMJitTileList * MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree);
