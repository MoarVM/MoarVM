#include "moar.h"
#include "dasm_proto.h"
#include "tile.h"

#define DECL_TILE(name) void MVM_jit_tile_ ## name (MVMThreadContext *tc, Dst_DECL)
DECL_TILE(load_stack);
DECL_TILE(load_local);
DECL_TILE(load_lbl);
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
DECL_TILE(label);
DECL_TILE(label_addr);
DECL_TILE(branch_label);

#include "x64_tile_tables.h"

/* The compilation process requires two primitives (at least):
 * - instruction selection
 * - register selection */

static void tile_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    MVMJitTile *tiles    = traverser->data;
    MVMJitExprNode op    = tree->nodes[node];
    const MVMJitExprOpInfo *info = tree->info[node].op;
    MVMint32 first_child = node+1;
    MVMint32 nchild      = info->nchild < 0 ? tree->nodes[first_child++] : info->nchild;
    MVMint32 state_idx, i;
    MVMint32 *state_info;
    switch (op) {
        /* TODO implement case for variadic nodes (DO/ALL/ANY/ARGLIST)
           and IF, which has 3 children */
    default:
        {
            if (nchild == 0) {
                state_idx = MVM_jit_tile_states_lookup(tc, op, -1, -1);
            } else if (nchild == 1) {
                MVMint32 left = tree->nodes[first_child];
                state_idx = MVM_jit_tile_states_lookup(tc, op, tiles[left].state, -1);
            } else {
                MVMint32 left  = tree->nodes[first_child];
                MVMint32 right = tree->nodes[first_child+1];
                state_idx = MVM_jit_tile_states_lookup(tc, op, tiles[left].state,
                                                       tiles[right].state);
            }
            if (state_idx < 0)
                MVM_oops(tc, "Tiler table could not find next state for %s\n",
                         info->name);
            state_info        = MVM_jit_tile_states[state_idx];
            tiles[node].state = state_info[3];
            tiles[node].rule  = MVM_jit_tile_rules[state_info[4]];
            for (i = 0; i < MAX(2,nchild); i++) {
                /* propagate child rules downward */
                MVMint32 child = tree->nodes[first_child+i];
                MVMint32 rule  = state_info[5+i];
                tiles[child].rule = MVM_jit_tile_rules[rule];
            }
            break;
        }
    }
}


void MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    traverser.inorder = NULL;
    traverser.preorder = NULL;
    traverser.postorder = &tile_node;
    traverser.data = MVM_calloc(tree->nodes_num, sizeof(MVMJitTile));
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    MVM_free(traverser.data);
}
