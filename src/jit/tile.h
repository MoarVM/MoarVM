struct MVMJitTile {
    void (*rule)(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree,
                    MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args);
    const MVMint8 *path;
};
