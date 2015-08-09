#include "moar.h"
#include "dasm_proto.h"
#include "x64_tile_decl.h"
#include "x64_tile_tables.h"



static void tile_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprNode op            = tree->nodes[node];
    MVMJitExprNodeInfo *symbol   = &tree->info[node];
    const MVMJitExprOpInfo *info = symbol->op_info;
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
                MVMint32 lstate = tree->info[left].tile_state;
                state_idx = MVM_jit_tile_states_lookup(tc, op, lstate, -1);
            } else if (nchild == 2) {
                MVMint32 left  = tree->nodes[first_child];
                MVMint32 lstate = tree->info[left].tile_state;
                MVMint32 right = tree->nodes[first_child+1];
                MVMint32 rstate = tree->info[left].tile_state;
                state_idx = MVM_jit_tile_states_lookup(tc, op, lstate, rstate);
            } else {
                MVM_oops(tc, "Can't deal with %d children of node %s\n", nchild, info->name);
            }
            if (state_idx < 0)
                MVM_oops(tc, "Tiler table could not find next state for %s\n",
                         info->name);
            state_info         = MVM_jit_tile_states[state_idx];
            symbol->tile_state = state_info[3];
            symbol->tile       = &MVM_jit_tile_table[state_info[4]];
            for (i = 0; i < MAX(2,nchild); i++) {
                /* propagate child rules downward */
                MVMint32 child   = tree->nodes[first_child+i];
                MVMint32 rule_nr = state_info[5+i];
                tree->info[child].tile = &MVM_jit_tile_table[rule_nr];
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
    traverser.data = NULL;
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
}

#define FIRST_CHILD(t,x) (t->info[x].op_info->nchild < 0 ? x + 2 : x + 1)
/* Get input for a tile rule, write into values */
void MVM_jit_tile_get_values(MVMThreadContext *tc, MVMJitExprTree *tree,
                             MVMint32 node, const MVMint8 *path,
                             MVMJitExprValue **values) {
    while (*path > 0) {
        MVMint32 cur_node = node;
        do {
            MVMint32 first_child = FIRST_CHILD(tree, cur_node) - 1;
            cur_node = tree->nodes[first_child+*path++];
        } while (*path > 0);
        *values++ = &tree->info[cur_node].value;
        path++;
    }
}

#undef FIRST_CHILD
