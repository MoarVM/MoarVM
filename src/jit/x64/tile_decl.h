#define MVM_JIT_TILE_DECL(name) \
    void MVM_jit_tile_ ## name (MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree, MVMint32 node, MVMJitExprValue **values, MVMJitExprNode *args)
MVM_JIT_TILE_DECL(load_stack);
MVM_JIT_TILE_DECL(load_local);
MVM_JIT_TILE_DECL(load_lbl);
MVM_JIT_TILE_DECL(load_cu);
MVM_JIT_TILE_DECL(load_tc);
MVM_JIT_TILE_DECL(load_frame);
MVM_JIT_TILE_DECL(load_vmnull);


MVM_JIT_TILE_DECL(addr);
MVM_JIT_TILE_DECL(idx);
MVM_JIT_TILE_DECL(const_reg);

MVM_JIT_TILE_DECL(load_reg);
MVM_JIT_TILE_DECL(load_addr);
MVM_JIT_TILE_DECL(load_idx);

MVM_JIT_TILE_DECL(cast);
MVM_JIT_TILE_DECL(cast_load_addr);

MVM_JIT_TILE_DECL(store);
MVM_JIT_TILE_DECL(store_addr);
MVM_JIT_TILE_DECL(store_idx);


MVM_JIT_TILE_DECL(add_reg);
MVM_JIT_TILE_DECL(add_const);
MVM_JIT_TILE_DECL(add_load_addr);
MVM_JIT_TILE_DECL(add_load_idx);

MVM_JIT_TILE_DECL(sub_reg);
MVM_JIT_TILE_DECL(sub_const);
MVM_JIT_TILE_DECL(sub_load_addr);
MVM_JIT_TILE_DECL(sub_load_idx);

MVM_JIT_TILE_DECL(and_reg);
MVM_JIT_TILE_DECL(and_const);
MVM_JIT_TILE_DECL(and_load_addr);
MVM_JIT_TILE_DECL(and_load_idx);


MVM_JIT_TILE_DECL(nz);
MVM_JIT_TILE_DECL(nz_addr);
MVM_JIT_TILE_DECL(nz_idx);
MVM_JIT_TILE_DECL(nz_and);
MVM_JIT_TILE_DECL(zr);

MVM_JIT_TILE_DECL(flagval);
MVM_JIT_TILE_DECL(copy);
MVM_JIT_TILE_DECL(all);
MVM_JIT_TILE_DECL(if);
MVM_JIT_TILE_DECL(either);
MVM_JIT_TILE_DECL(do_reg);
MVM_JIT_TILE_DECL(do_void);
MVM_JIT_TILE_DECL(when);

MVM_JIT_TILE_DECL(label);
MVM_JIT_TILE_DECL(branch_label);

MVM_JIT_TILE_DECL(cmp);

MVM_JIT_TILE_DECL(call);
MVM_JIT_TILE_DECL(call_func);
MVM_JIT_TILE_DECL(call_addr);

