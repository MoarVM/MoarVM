#include "moar.h"
#include "dasm_proto.h"
#define DECL_TILE(name) void MVM_jit_tile_ ## name (MVMThreadContext *tc, Dst_DECL)
DECL_TILE(load_stack);
DECL_TILE(load_local);
DECL_TILE(load_cu);
DECL_TILE(load_tc);
DECL_TILE(load_frame);
DECL_TILE(addr_mem);
DECL_TILE(addr_reg);
DECL_TILE(idx_mem);
DECL_TILE(idx_reg);
DECL_TILE(const_reg);
DECL_TILE(load_reg);
DECL_TILE(load_mem);
DECL_TILE(store_reg);
DECL_TILE(store_mem);
DECL_TILE(add_reg);
DECL_TILE(add_const);
DECL_TILE(add_load_mem);
DECL_TILE(sub_reg);
DECL_TILE(sub_const);
DECL_TILE(sub_load_mem);
DECL_TILE(and_reg);
DECL_TILE(and_const);
DECL_TILE(and_load_mem);
DECL_TILE(nz_reg);
DECL_TILE(nz_mem);
DECL_TILE(nz_and);
DECL_TILE(all);
DECL_TILE(if);
DECL_TILE(if_all);
DECL_TILE(do_reg);
DECL_TILE(do_void);
DECL_TILE(when);
DECL_TILE(when_all);
DECL_TILE(when_branch);
DECL_TILE(label_reg);
DECL_TILE(branch_label);


/* The compilation process requires two primitives (at least):
 * - instruction selection
 * - register selection
 *
 * It would seem logical to output
 */

