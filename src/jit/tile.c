#include "moar.h"
#include "internal.h"
#include <math.h>

#if MVM_JIT_ARCH == MVM_JIT_ARCH_X64
#include "x64/tile_decl.h"
#include "x64/tile_tables.h"
#endif

struct TileState {
    /* state is junction of applicable tiles */
    MVMint32 state;
    /* rule is number of best applicable tile */
    MVMint32 rule;
    /* template is template of assigned tile */
    const MVMJitTileTemplate *template;
};

struct TreeTiler {
    MVM_VECTOR_DECL(struct TileState, states);
    MVMJitCompiler *compiler;
    MVMJitTileList *list;
};

/* Postorder collection of tile states (rulesets) */
static void tile_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node) {
    struct TreeTiler *tiler      = traverser->data;
    MVMJitExprNode op            = tree->nodes[node];
    const MVMJitExprOpInfo *info = tree->info[node].op_info;
    MVMint32 first_child = node+1;
    MVMint32 nchild      = info->nchild < 0 ? tree->nodes[first_child++] : info->nchild;
    MVMint32 *state_info = NULL;

    switch (op) {
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
    case MVM_JIT_ARGLIST:
        {
            /* Unary variadic nodes are exactly the same... */
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                MVMint32 child = tree->nodes[first_child+i];
                state_info = MVM_jit_tile_state_lookup(tc, op, tiler->states[child].state, -1);
                if (state_info == NULL) {
                    MVM_oops(tc, "OOPS, %s can't be tiled with a %s child at position %d",
                             info->name, tree->info[child].op_info->name, i);
                }
            }
            tiler->states[node].state    = state_info[3];
            tiler->states[node].rule     = state_info[4];
        }
        break;
    case MVM_JIT_DO:
        {
            MVMint32 last_child = first_child+nchild-1;
            MVMint32 left_state = tiler->states[tree->nodes[first_child]].state;
            MVMint32 right_state = tiler->states[tree->nodes[last_child]].state;
            state_info = MVM_jit_tile_state_lookup(tc, op, left_state, right_state);
            if (state_info == NULL) {
                MVM_oops(tc, "Can't tile this DO node");
            }
            tiler->states[node].state = state_info[3];
            tiler->states[node].rule  = state_info[4];
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            MVMint32 cond = tree->nodes[node+1],
                left = tree->nodes[node+2],
                right = tree->nodes[node+3];
            MVMint32 *left_state  = MVM_jit_tile_state_lookup(tc, op, tiler->states[cond].state,
                                                              tiler->states[left].state);
            MVMint32 *right_state = MVM_jit_tile_state_lookup(tc, op, tiler->states[cond].state,
                                                              tiler->states[right].state);
            if (left_state == NULL || right_state == NULL ||
                left_state[3] != right_state[3] ||
                left_state[4] != right_state[4]) {
                MVM_oops(tc, "Inconsistent %s tile state", info->name);
            }
            tiler->states[node].state = left_state[3];
            tiler->states[node].rule  = left_state[4];
        }
        break;
    default:
        {
            if (nchild == 0) {
                state_info = MVM_jit_tile_state_lookup(tc, op, -1, -1);
            } else if (nchild == 1) {
                MVMint32 left = tree->nodes[first_child];
                MVMint32 lstate = tiler->states[left].state;
                state_info = MVM_jit_tile_state_lookup(tc, op, lstate, -1);
            } else if (nchild == 2) {
                MVMint32 left  = tree->nodes[first_child];
                MVMint32 lstate = tiler->states[left].state;
                MVMint32 right = tree->nodes[first_child+1];
                MVMint32 rstate = tiler->states[right].state;
                state_info = MVM_jit_tile_state_lookup(tc, op, lstate, rstate);
            } else {
                MVM_oops(tc, "Can't deal with %d children of node %s\n", nchild, info->name);
            }
            if (state_info == NULL)
                MVM_oops(tc, "Tiler table could not find next state for %s\n",
                         info->name);
            tiler->states[node].state = state_info[3];
            tiler->states[node].rule  = state_info[4];
        }
    }
}


/* It may happen that a nodes which is used multiple times is tiled in
 * differrent ways, because it is the parent tile which determines which
 * 'symbol' the child node gets to implement, and hence different parents might
 * decide differently. That may mean the same value will be computed more than
 * once, which could be suboptimal. Still, it is necessary to resolve such
 * conflicts. We do so by generating a new node for the parent node to refer to,
 * leaving the old node as it was. That may cause the tree to grow, which is
 * implemented by realloc. As a result, it is unsafe to take references to tree
 * elements while it is being modified. */

static MVMint32 assign_tile(MVMThreadContext *tc, MVMJitExprTree *tree,
                            MVMJitTreeTraverser *traverser,
                            MVMJitExprNode node, MVMint32 rule_nr) {
    const MVMJitTileTemplate *template = &MVM_jit_tile_templates[rule_nr];
    struct TreeTiler *tiler = traverser->data;

    if (rule_nr > (sizeof(MVM_jit_tile_templates)/sizeof(MVM_jit_tile_templates[0])))
        MVM_oops(tc, "Attempt to assign invalid tile rule %d\n", rule_nr);

    if (tiler->states[node].template == NULL || tiler->states[node].template == template ||
        memcmp(template, tiler->states[node].template, sizeof(MVMJitTileTemplate)) == 0) {
        /* happy case, no conflict */
        tiler->states[node].rule     = rule_nr;
        tiler->states[node].template = template;
        return node;
    } else {
        /* resolve conflict by copying this node */
        const MVMJitExprOpInfo *info = tree->info[node].op_info;
        MVMint32 space = (info->nchild < 0 ?
                          2 + tree->nodes[node+1] + info->nargs :
                          1 + info->nchild + info->nargs);
        MVMint32 num   = tree->nodes_num;

        /* NB - we should have an append_during_traversal function
         * because the following is quite a common pattern */

        /* Internal copy; hence no realloc may happen during append, ensure the
         * space is available before the copy */
        MVM_VECTOR_ENSURE_SPACE(tree->nodes, space);
        MVM_VECTOR_APPEND(tree->nodes, tree->nodes + node, space);

        /* Copy the information node as well */
        MVM_VECTOR_ENSURE_SIZE(tree->info, num);
        memcpy(tree->info + num, tree->info + node, sizeof(MVMJitExprNodeInfo));

        /* Also ensure the visits and tiles array are of correct size */
        MVM_VECTOR_ENSURE_SIZE(tiler->states, num);
        MVM_VECTOR_ENSURE_SIZE(traverser->visits, num);

        /* Assign the new tile */
        tiler->states[num].rule     = rule_nr;
        tiler->states[num].template = template;

        /* Return reference to new node */
        return num;
    }
}



/* Preorder propagation of rules downward */
static void select_tiles(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node) {

    MVMJitExprNode op    = tree->nodes[node];
    MVMint32 first_child = node+1;
    MVMint32 nchild      = (tree->info[node].op_info->nchild < 0 ?
                            tree->nodes[first_child++] :
                            tree->info[node].op_info->nchild);
    struct TreeTiler *tiler = traverser->data;

    const MVMJitTileTemplate *tile = tiler->states[node].template;
    MVMint32 left_sym = tile->left_sym, right_sym = tile->right_sym;

/* Tile assignment is somewhat precarious due to (among other things), possible
 * reallocation. So let's provide a single macro to do it correctly. */
#define DO_ASSIGN_CHILD(NUM, SYM) do { \
        MVMint32 child     = tree->nodes[first_child+(NUM)]; \
        MVMint32 state     = tiler->states[child].state; \
        MVMint32 rule      = MVM_jit_tile_select_lookup(tc, state, (SYM)); \
        MVMint32 assigned  = assign_tile(tc, tree, traverser, child, rule); \
        tree->nodes[first_child+(NUM)] = assigned; \
    } while(0)

    switch (op) {
    case MVM_JIT_ALL:
    case MVM_JIT_ANY:
    case MVM_JIT_ARGLIST:
        {
            MVMint32 i;
            for (i = 0; i < nchild; i++) {
                DO_ASSIGN_CHILD(i, left_sym);
            }
        }
        break;
    case MVM_JIT_DO:
        {
            MVMint32 i, last_child, last_rule;
            for (i = 0; i < nchild - 1; i++) {
                DO_ASSIGN_CHILD(i, left_sym);
            }
            DO_ASSIGN_CHILD(i, right_sym);
        }
        break;
    case MVM_JIT_IF:
    case MVM_JIT_EITHER:
        {
            DO_ASSIGN_CHILD(0, left_sym);
            DO_ASSIGN_CHILD(1, right_sym);
            DO_ASSIGN_CHILD(2, right_sym);
        }
        break;
    default:
        {
            if (nchild > 0) {
                DO_ASSIGN_CHILD(0, left_sym);
            }
            if (nchild > 1) {
                DO_ASSIGN_CHILD(1, right_sym);
            }
            if (nchild > 2) {
                MVM_oops(tc, "Can't tile %d children of %s", nchild, tree->info[node].op_info->name);
            }
        }
    }
#undef DO_ASSIGN_CHILD
    /* (Currently) we never insert into the tile list here */
}

/* Make complete tiles. Note that any argument passed is interpreted as an
 * int32. Used primarily for making 'synthetic' tiles introduced by the
 * compiler */
MVMJitTile* MVM_jit_tile_make(MVMThreadContext *tc, MVMJitCompiler *compiler,
                              void *emit, MVMint32 num_args, MVMint32 num_values, ...) {
    MVMJitTile *tile;
    MVMint32 i;
    va_list arglist;
    va_start(arglist, num_refs);
    tile = MVM_spesh_alloc(tc, compiler->graph->sg, sizeof(MVMJitTile));
    tile->emit = emit;
    tile->num_refs = num_values;
    for (i = 0; i < num_args; i++) {
        tile->args[i] = va_arg(arglist, MVMint32);
    }
    for (i = 0; i < num_values; i++) {
        tile->values[i] = (MVMint8)va_arg(arglist, MVMint32);
    }
    va_end(arglist);
    return tile;
}

/* Insert labels, compute basic block extents (eventually) */
static void build_blocks(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node, MVMint32 i) {
    struct TreeTiler *tiler = traverser->data;
    switch (tree->nodes[node]) {
    case MVM_JIT_WHEN:
    {
        MVMint32 when_label = tree->info[node].label;
        if (i == 0) {
            MVMint32 test  = tree->nodes[node+1];
            MVMint32 flag  = tree->nodes[test];
            /* First child is the test */
            if (flag == MVM_JIT_ALL) {
                /* Do nothing, shortcircuit of ALL has skipped the conditional
                   block if necessary */
            } else if (flag == MVM_JIT_ANY) {
                /* If ANY hasn't short-circuited into the conditional block,
                   jump after it */
                MVMint32 any_label = tree->info[test].label;
                MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_branch,
                                                       1, 0, when_label);
                MVMJitTile *label  = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                       1, 0, any_label);
                MVM_VECTOR_PUSH(tiler->list->items, branch);
                /* Compile label for the left block entry */
                MVM_VECTOR_PUSH(tiler->list->items, label);
            } else {
                /* Other tests require a conditional branch */
                MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_conditional_branch,
                                                       2, 0, MVM_jit_expr_op_negate_flag(tc, flag), when_label);
                MVM_VECTOR_PUSH(tiler->list->items, branch);
            }
        } else {
            /* after child of WHEN, insert the label */
            MVMJitTile *label = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                  1, 0, when_label);
            MVM_VECTOR_PUSH(tiler->list->items, label);
        }
        break;
    }
    case MVM_JIT_ALL:
    {
        MVMint32 test = tree->nodes[node+2+i];
        MVMint32 flag = tree->nodes[test];
        MVMint32 all_label = tree->info[node].label;
        if (flag == MVM_JIT_ALL) {
            /* Nested ALL short-circuits identically */
        } else if (flag == MVM_JIT_ANY) {
            /* If ANY reached it's end, that means it's false. So branch out */
            MVMint32 any_label = tree->info[test].label;
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_branch, 1, 0, all_label);
            MVMJitTile *label  = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label, 1, 0, any_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
            /* And if ANY short-circuits we should continue the evaluation of ALL */
            MVM_VECTOR_PUSH(tiler->list->items, label);
        } else {
            /* Flag should be negated (ALL = short-circiut unless condition)) */
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler,
                                                   MVM_jit_compile_conditional_branch, 2, 0,
                                                   MVM_jit_expr_op_negate_flag(tc, flag), all_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
        }
        break;
    }
    case MVM_JIT_ANY:
    {
        MVMint32 test  = tree->nodes[node+2+i];
        MVMint32 flag  = tree->nodes[test];
        MVMint32 any_label = tree->info[node].label;
        if (flag == MVM_JIT_ALL) {
            /* If ALL reached the end, it must have been succesful, and ANY's
               short-circuit behaviour implies we should branch out */
            MVMint32 all_label = tree->info[test].label;
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_branch,
                                                   1, 0, any_label);
            MVMJitTile *label  = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                   1, 0, all_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
            /* If not succesful, testing should continue (thus ALL must branch into our ANY) */
            MVM_VECTOR_PUSH(tiler->list->items, label);
        } else if (flag == MVM_JIT_ANY) {
            /* Nothing to do here, since nested ANY already short-circuits to
               our label */
        } else {
            /* Normal evaluation (ANY = short-circuit if condition) */
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler,
                                                   MVM_jit_compile_conditional_branch,
                                                   2, 0, flag, any_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
        }
        break;
    }
    case MVM_JIT_EITHER:
    case MVM_JIT_IF:
    {
        MVMint32 left_label = tree->info[node].label;
        MVMint32 right_label = left_label + 1;
        if (i == 0) {
            /* after flag child */
            MVMint32 test = tree->nodes[node+1];
            MVMint32 flag = tree->nodes[test];
            if (flag == MVM_JIT_ALL) {
                /* If we reach this code then ALL was true, hence we should
                 * enter the left block, and do nothing */
            } else if (flag == MVM_JIT_ANY) {
                /* We need the branch to the right block and the label for ANY
                 * to jump to enter the left block */
                MVMint32 any_label = tree->info[test].label;
                MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_branch,
                                                       1, 0, left_label);
                MVMJitTile *label  = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                       1, 0, any_label);
                MVM_VECTOR_PUSH(tiler->list->items, branch);
                MVM_VECTOR_PUSH(tiler->list->items, label);
            } else {
                MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler,
                                                       MVM_jit_compile_conditional_branch, 2, 0,
                                                       MVM_jit_expr_op_negate_flag(tc, flag), left_label);
                MVM_VECTOR_PUSH(tiler->list->items, branch);
            }
        } else if (i == 1) {
            /* between left and right conditional block */
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_branch,
                                                   1, 0, right_label);
            MVMJitTile *label  = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                   1, 0, left_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
            MVM_VECTOR_PUSH(tiler->list->items, label);
        } else {
            /* after 'right' conditional block */
            MVMJitTile *branch = MVM_jit_tile_make(tc, tiler->compiler, MVM_jit_compile_label,
                                                   1, 0, right_label);
            MVM_VECTOR_PUSH(tiler->list->items, branch);
        }
    }
    default:
        break;
    }
}


static void build_tilelist(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                           MVMJitExprTree *tree, MVMint32 node) {
    struct TreeTiler *tiler = traverser->data;
    const MVMJitTileTemplate *template = tiler->states[node].template;
    MVMJitTile *tile;

    /* only need to add 'real' tiles; emit may be null for a definition */
    if (template->expr == NULL)
        return;

    tile = MVM_jit_tile_make_from_template(tc, tiler->compiler, template, tree, node);

    MVM_VECTOR_PUSH(tiler->list->items, tile);
    /* count tne number of refs for ARGLIST */
    if (tile->op == MVM_JIT_ARGLIST) {
        tiler->list->num_arglist_refs += tile->num_refs;
    }
}

/* Create a tile from a template */
MVMJitTile * MVM_jit_tile_make_from_template(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                             const MVMJitTileTemplate *template,
                                             MVMJitExprTree *tree, MVMint32 node) {
    MVMJitTile *tile;
    tile = MVM_spesh_alloc(tc, compiler->graph->sg, sizeof(MVMJitTile));
    tile->emit          = template->emit;
    tile->register_spec = template->register_spec;
    tile->node          = node;
    tile->op            = tree->nodes[node];
    tile->size          = tree->info[node].size;

    /* Assign tile arguments and compute the refering nodes */
    switch (tile->op) {
    case MVM_JIT_IF:
    {
        tile->refs[0] = tree->nodes[node+2];
        tile->refs[1] = tree->nodes[node+3];
        tile->num_refs = 2;
        break;
    }
    case MVM_JIT_ARGLIST:
    {
        /* because arglist needs special-casing and because it will use more
         * than 8 (never mind 4) values, it won't fit into refs, so we're not
         * reading them here */
        tile->num_refs = tree->nodes[node+1];
        break;
    }
    case MVM_JIT_DO:
    {
        MVMint32 nchild  = tree->nodes[node+1];
        tile->refs[0]    = tree->nodes[node+1+nchild];
        tile->num_refs = 1;
        break;
    }
    default:
    {
        MVMint32 i, j, k, num_nodes, value_bitmap;
        MVMJitExprNode buffer[8];
        num_nodes        = MVM_jit_expr_tree_get_nodes(tc, tree, node, template->path, buffer);
        value_bitmap     = template->value_bitmap;
        tile->num_refs   = template->num_refs;
        j = 0;
        k = 0;
        /* splice out args from node refs */
        for (i = 0; i < num_nodes; i++) {
            if (value_bitmap & 1) {
                tile->refs[j++] = buffer[i];
            } else {
                tile->args[k++] = buffer[i];
            }
            value_bitmap >>= 1;
        }
        break;
    }
    }
    return tile;
}

MVMJitTileList * MVM_jit_tile_expr_tree(MVMThreadContext *tc, MVMJitCompiler *compiler, MVMJitExprTree *tree) {
    MVMJitTreeTraverser traverser;
    MVMint32 i;
    struct TreeTiler tiler;

    MVM_VECTOR_INIT(tiler.states, tree->nodes_num);
    traverser.policy = MVM_JIT_TRAVERSER_ONCE;
    traverser.inorder = NULL;
    traverser.preorder = NULL;
    traverser.postorder = &tile_node;
    traverser.data = &tiler;

    MVM_jit_expr_tree_traverse(tc, tree, &traverser);
    /* 'pushdown' of tiles to roots */
    for (i = 0; i < tree->roots_num; i++) {
        MVMint32 node = tree->roots[i];
        assign_tile(tc, tree, &traverser, tree->roots[i], tiler.states[node].rule);
    }

    /* Create serial list of actual tiles which represent the final low-level code */
    tiler.compiler      = compiler;
    tiler.list          = MVM_spesh_alloc(tc, compiler->graph->sg, sizeof(MVMJitTileList));
    tiler.list->tree    = tree;
    tiler.list->num_arglist_refs = 0;

    MVM_VECTOR_INIT(tiler.list->items, tree->nodes_num / 2);
    MVM_VECTOR_INIT(tiler.list->inserts, 0);


    traverser.preorder  = &select_tiles;
    traverser.inorder   = &build_blocks;
    traverser.postorder = &build_tilelist;

    MVM_jit_expr_tree_traverse(tc, tree, &traverser);

    MVM_free(tiler.states);
    return tiler.list;
}


static int cmp_tile_insert(const void *p1, const void *p2) {
    const struct MVMJitTileInsert *a = p1, *b = p2;
    return a->position == b->position ?
        a->order - b->order :
        a->position - b->position;
}


void MVM_jit_tile_list_insert(MVMThreadContext *tc, MVMJitTileList *list, MVMJitTile *tile, MVMint32 position, MVMint32 order) {
    struct MVMJitTileInsert i = { position, order, tile };
    MVM_VECTOR_PUSH(list->inserts, i);
}

void MVM_jit_tile_list_edit(MVMThreadContext *tc, MVMJitTileList *list) {
    MVMJitTile **worklist;
    MVMint32 i, j, k;
    if (list->inserts_num == 0)
        return;

    /* sort inserted tiles in ascending order */
    qsort(list->inserts, list->inserts_num,
          sizeof(struct MVMJitTileInsert), cmp_tile_insert);

    /* create a new array for the tiles */
    worklist = MVM_malloc((list->items_num + list->inserts_num) * sizeof(MVMJitTile*));

    i = 0; /* items */
    j = 0; /* inserts */
    k = 0; /* output */

    while (i < list->items_num) {
        while (j < list->inserts_num &&
               list->inserts[j].position < i) {
            worklist[k++] = list->inserts[j++].tile;
        }
        worklist[k++] = list->items[i++];
    }
    /* insert all tiles after the last one, if any */
    while (j < list->inserts_num) {
        worklist[k++] = list->inserts[j++].tile;
    }

    /* swap old and new list */
    MVM_free(list->items);
    list->items = worklist;
    list->items_num = k;
    list->items_alloc = k;

    /* Cleanup edits buffer */
    MVM_free(list->inserts);
    MVM_VECTOR_INIT(list->inserts, 0);
}

void MVM_jit_tile_list_destroy(MVMThreadContext *tc, MVMJitTileList *list) {
    MVM_free(list->items);
    MVM_free(list->inserts);
}
