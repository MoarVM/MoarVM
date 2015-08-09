struct MVMJitTile {
    void (*rule)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                    MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args);
    const MVMint8 *path;
    MVMJitExprVtype vtype;
};

void MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
void MVM_jit_tile_get_values(MVMThreadContext *tc, MVMJitExprTree *tree, MVMint32 node,
                             const MVMint8 *path, MVMJitExprValue **values);
