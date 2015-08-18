#include "moar.h"
#include "dasm_proto.h"
#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
#include "x64/tile_decl.h"
#include "x64/tile_tables.h"
#endif


/* Postorder collection of tile states (rulesets) */
static void tile_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprNode op            = tree->nodes[node];
    MVMJitExprNodeInfo *symbol   = &tree->info[node];
    const MVMJitExprOpInfo *info = symbol->op_info;
    MVMint32 first_child = node+1;
    MVMint32 nchild      = info->nchild < 0 ? tree->nodes[first_child++] : info->nchild;
    MVMint32 *state_info = NULL;
    if (traverser->visits[node] > 1)
        return;
    switch (op) {
        /* TODO implement case for variadic nodes (DO/ALL/ANY/ARGLIST)
           and IF, which has 3 children */
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
    case MVM_JIT_ARGLIST:
        {
            /* Unary variadic nodes are exactly the same... */
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                MVMint32 child = tree->nodes[first_child+i];
                state_info = MVM_jit_tile_state_lookup(tc, op, tree->info[child].tile_state, -1);
                if (state_info == NULL) {
                    MVM_oops(tc, "OOPS, %s can't be tiled with a %s child at position %d",
                             info->name, tree->info[child].op_info->name, i);
                }
            }
            symbol->tile_state = state_info[3];
            symbol->tile_rule  = state_info[4];
        }
        break;
    case MVM_JIT_DO:
        {
            MVMint32 last_child = tree->nodes[first_child+nchild-1];
            state_info = MVM_jit_tile_state_lookup(tc, op, tree->info[first_child].tile_state,
                                                   tree->info[last_child].tile_state);
            if (state_info == NULL) {
                MVM_oops(tc, "Can't tile this DO node");
            }
            symbol->tile_state = state_info[3];
            symbol->tile_rule  = state_info[4];
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 cond = tree->nodes[node+1],
                left = tree->nodes[node+2],
                right = tree->nodes[node+3];
            MVMint32 *left_state  = MVM_jit_tile_state_lookup(tc, op, tree->info[cond].tile_state,
                                                              tree->info[left].tile_state);
            MVMint32 *right_state = MVM_jit_tile_state_lookup(tc, op, tree->info[cond].tile_state,
                                                              tree->info[right].tile_state);
            if (left_state == NULL || right_state == NULL ||
                left_state[3] != right_state[3] ||
                left_state[4] != right_state[4]) {
                MVM_oops(tc, "Inconsistent %s tile state", info->name);
            }
            symbol->tile_state = left_state[3];
            symbol->tile_rule  = left_state[4];
        }
        break;
    default:
        {
            if (nchild == 0) {
                state_info = MVM_jit_tile_state_lookup(tc, op, -1, -1);
            } else if (nchild == 1) {
                MVMint32 left = tree->nodes[first_child];
                MVMint32 lstate = tree->info[left].tile_state;
                state_info = MVM_jit_tile_state_lookup(tc, op, lstate, -1);
            } else if (nchild == 2) {
                MVMint32 left  = tree->nodes[first_child];
                MVMint32 lstate = tree->info[left].tile_state;
                MVMint32 right = tree->nodes[first_child+1];
                MVMint32 rstate = tree->info[right].tile_state;
                state_info = MVM_jit_tile_state_lookup(tc, op, lstate, rstate);
            } else {
                MVM_oops(tc, "Can't deal with %d children of node %s\n", nchild, info->name);
            }
            if (state_info == NULL)
                MVM_oops(tc, "Tiler table could not find next state for %s\n",
                         info->name);
            symbol->tile_state = state_info[3];
            symbol->tile_rule  = state_info[4];
        }
    }
}


static MVMint32 assign_tile(MVMThreadContext *tc, MVMJitExprTree *tree,
                            MVMJitExprNode node, MVMint32 tile_rule) {
    const MVMJitTile *tile = &MVM_jit_tile_table[tile_rule];
    if (tile_rule > (sizeof(MVM_jit_tile_table)/sizeof(MVM_jit_tile_table[0])))
        MVM_oops(tc, "What, trying to assign tile rule %d\n", tile_rule);
    if (tree->info[node].tile == NULL || tree->info[node].tile == tile ||
        memcmp(tile, tree->info[node].tile, sizeof(MVMJitTile)) == 0) {
        /* happy case, no conflict */
        tree->info[node].tile_rule = tile_rule;
        tree->info[node].tile      = tile;
        return node;
    } else {
        /* resolve conflict by copying this node */
        const MVMJitExprOpInfo *info = tree->info[node].op_info;
        MVMint32 space = (info->nchild < 0 ?
                          2 + tree->nodes[node+1] + info->nargs :
                          1 + info->nchild + info->nargs);
        MVMint32 num   = tree->nodes_num;
        /* Internal copy, so ensure space is available (no realloc may
           happen during append), so we don't try to read from memory
           freed in the realloc */
        MVM_DYNAR_ENSURE_SPACE(tree->nodes, space);
        MVM_DYNAR_APPEND(tree->nodes, tree->nodes + node, space);
        /* Copy the information node */
        MVM_DYNAR_ENSURE_SIZE(tree->info, num);
        memcpy(tree->info + num, tree->info + node, sizeof(MVMJitExprNodeInfo));
        /* Assign the new tile */
        tree->info[num].tile_rule = tile_rule;
        tree->info[num].tile      = tile;
        /* Return reference to new node */
        return num;
    }
}



/* Preorder propagation of rules downward */
static void select_tiles(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node) {
    MVMJitExprNode op               = tree->nodes[node];
    MVMJitExprNodeInfo *info        = &tree->info[node];
    const MVMJitExprOpInfo *op_info = info->op_info;
    MVMint32 first_child = node+1;
    MVMint32 nchild      = op_info->nchild < 0 ? tree->nodes[first_child++] : op_info->nchild;
    if (traverser->visits[node] > 1)
        return;
    switch (op) {
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
    case MVM_JIT_ARGLIST:
        {
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                MVMint32 child = tree->nodes[first_child+i];
                MVMint32 rule  = MVM_jit_tile_select_lookup(tc, tree->info[child].tile_state, info->tile->left_sym);
                tree->nodes[first_child+i] = assign_tile(tc, tree, child, rule);
            }
        }
        break;
    case MVM_JIT_DO:
        {
            MVMint32 i, last_child, last_rule;
            for (i = 0; i < nchild - 1; i++) {
                MVMint32 child = tree->nodes[first_child+i];
                MVMint32 rule  = MVM_jit_tile_select_lookup(tc, tree->info[child].tile_state, info->tile->left_sym);
                tree->nodes[first_child+i] = assign_tile(tc, tree, child, rule);
            }
            last_child = tree->nodes[first_child+i];
            last_rule  = MVM_jit_tile_select_lookup(tc, tree->info[last_child].tile_state, info->tile->right_sym);
            tree->nodes[first_child+i] = assign_tile(tc, tree, last_child, last_rule);
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 cond = tree->nodes[first_child],
                left  = tree->nodes[first_child+1],
                right = tree->nodes[first_child+2],
                rule;

            rule = MVM_jit_tile_select_lookup(tc, tree->info[cond].tile_state, info->tile->left_sym);
            tree->nodes[first_child]   = assign_tile(tc, tree, cond, rule);

            rule = MVM_jit_tile_select_lookup(tc, tree->info[left].tile_state, info->tile->right_sym);
            tree->nodes[first_child+1] = assign_tile(tc, tree, left, rule);

            rule = MVM_jit_tile_select_lookup(tc, tree->info[right].tile_state, info->tile->right_sym);
            tree->nodes[first_child+2] = assign_tile(tc, tree, right, rule);
        }
        break;
    default:
        {
            /* Assign tiles to children */
            if (nchild > 0) {
                MVMint32 left = tree->nodes[first_child];
                MVMint32 rule = MVM_jit_tile_select_lookup(tc, tree->info[left].tile_state, info->tile->left_sym);
                tree->nodes[first_child] = assign_tile(tc, tree, left, rule);
            }
            if (nchild > 1) {
                MVMint32 right = tree->nodes[first_child+1];
                MVMint32 rule  = MVM_jit_tile_select_lookup(tc, tree->info[right].tile_state, info->tile->right_sym);
                tree->nodes[first_child+1] = assign_tile(tc, tree, right, rule);
            }
            if (nchild > 2) {
                MVM_oops(tc, "Can't tile %d children of %s", nchild, op_info->name);
            }
        }
    }
    /* Ensure that the visits array grows along with the tree */
    MVM_DYNAR_ENSURE_SIZE(traverser->visits, tree->nodes_num);
}


static void log_tile(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                     MVMJitExprTree *tree, MVMint32 node) {
    if (traverser->visits[node] > 1)
        return;
    MVM_jit_log(tc, "%04d: %s\n", node, tree->info[node].tile->descr);
}


void MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMint32 i;
    traverser.inorder = NULL;
    traverser.preorder = NULL;
    traverser.postorder = &tile_node;
    traverser.data = NULL;
    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    for (i = 0; i < tree->roots_num; i++) {
        MVMint32 node = tree->roots[i];
        assign_tile(tc, tree, node, tree->info[node].tile_rule);
    }
    /* NB - we can add actual code generation during the postorder step here */
    traverser.preorder  = &select_tiles;
    traverser.postorder = &log_tile;
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
