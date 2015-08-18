struct MVMJitTile {
    void (*rule)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                    MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args);
    const MVMint8 *path;
    const char    *descr;
    MVMint32  num_values;
    MVMJitExprVtype vtype;
    MVMint32  left_sym;
    MVMint32 right_sym;
};

void MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
void MVM_jit_tile_get_values(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 node,
                             const MVMint8 *path, MVMJitExprValue **values);
