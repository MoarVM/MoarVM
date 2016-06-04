struct MVMJitTileTemplate {
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                 MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args);
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
    void (*emit)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                 MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args);
    MVMint32 node;
    MVMint32 num_values;
    /* buffers for the args of this (pseudo) tile */
    MVMJitExprValue *values[8];
    MVMJitExprNode args[8];
};

struct MVMJitTileList {
    MVMJitExprTree *tree;
    MVM_DYNAR_DECL(MVMJitTile*, items);
};

MVMJitTileList * MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
