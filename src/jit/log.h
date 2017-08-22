void MVM_jit_log(MVMThreadContext *tc, const char *fmt, ...) MVM_FORMAT(printf, 2, 3);
void MVM_jit_log_bytecode(MVMThreadContext *tc, MVMJitCode *code);
void MVM_jit_log_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
void MVM_jit_log_tile_list(MVMThreadContext *tc, MVMJitTileList *list);
