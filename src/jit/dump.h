void MVM_jit_dump_bytecode(MVMThreadContext *tc, MVMJitCode *code);
void MVM_jit_dump_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
void MVM_jit_dump_tile_list(MVMThreadContext *tc, MVMJitTileList *list);

MVM_STATIC_INLINE MVMint32 MVM_jit_debug_enabled(MVMThreadContext *tc) {
    return MVM_spesh_debug_enabled(tc) && tc->instance->jit_debug_enabled;
}

MVM_STATIC_INLINE MVMint32 MVM_jit_bytecode_dump_enabled(MVMThreadContext *tc) {
    return tc->instance->jit_bytecode_dir != NULL;
}
