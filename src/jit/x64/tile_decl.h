MVM_JIT_TILE_DECL(addr);
MVM_JIT_TILE_DECL(idx);
MVM_JIT_TILE_DECL(const_reg);
MVM_JIT_TILE_DECL(const_large);

MVM_JIT_TILE_DECL(load_lbl);
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

MVM_JIT_TILE_DECL(or_reg);
MVM_JIT_TILE_DECL(xor_reg);
MVM_JIT_TILE_DECL(not_reg);

MVM_JIT_TILE_DECL(test);
MVM_JIT_TILE_DECL(test_addr);
MVM_JIT_TILE_DECL(test_idx);
MVM_JIT_TILE_DECL(test_and);
MVM_JIT_TILE_DECL(test_const);
MVM_JIT_TILE_DECL(test_addr_const);

MVM_JIT_TILE_DECL(cmp);
MVM_JIT_TILE_DECL(flagval);

MVM_JIT_TILE_DECL(mark);
MVM_JIT_TILE_DECL(label);
MVM_JIT_TILE_DECL(branch_label);

MVM_JIT_TILE_DECL(call);
MVM_JIT_TILE_DECL(call_func);
MVM_JIT_TILE_DECL(call_addr);
