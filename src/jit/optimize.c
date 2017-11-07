#include "moar.h"

#if defined(MVM_JIT_DEBUG) && (MVM_JIT_DEBUG & MVM_JIT_DEBUG_OPTIMIZER) != 0
#define _DEBUG(fmt, ...) do { MVM_jit_log(tc, fmt "%s", __VA_ARGS__, "\n"); } while(0)
#else
#define _DEBUG(fmt, ...) do {} while(0)
#endif

struct NodeRef {
    MVMint32 parent;
    MVMint32 ptr;
    MVMint32 next;
};

struct NodeInfo {
    MVMint32 refs;
    MVMint32 ref_cnt;
};

struct Optimizer {
    MVM_VECTOR_DECL(struct NodeRef, refs);
    MVM_VECTOR_DECL(struct NodeInfo, info);
    MVM_VECTOR_DECL(MVMint32, replacements);
    MVMint32 replacement_cnt;
};

static void optimize_preorder(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                              MVMJitExprTree *tree, MVMint32 node) {
    /* i can imagine that there might be interesting optimizations we could apply here */

}

static void replace_node(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                         MVMJitExprTree *tree, MVMint32 node, MVMint32 replacement) {
    MVMint32 *c;
    struct Optimizer *o = traverser->data;
    /* double pointer iteration to update all children */
    _DEBUG("Replaced node %d with %d", node, replacement);

    MVM_VECTOR_ENSURE_SIZE(traverser->visits, replacement);
    MVM_VECTOR_ENSURE_SIZE(o->info, replacement);
    MVM_VECTOR_ENSURE_SIZE(o->replacements, replacement);

    for (c = &o->info[node].refs; *c > 0; c = &o->refs[*c].next) {
        tree->nodes[o->refs[*c].ptr] = replacement;
    }

    /* append existing to list */
    if (o->info[replacement].refs > 0) {
        *c = o->info[replacement].refs;
    }
    o->info[replacement].refs = o->info[node].refs;
    o->info[replacement].ref_cnt += o->info[node].ref_cnt;

    o->replacements[node] = replacement;
    o->replacement_cnt++;
}

static void optimize_child(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                           MVMJitExprTree *tree, MVMint32 node, MVMint32 child) {
    /* add reference from parent to child, replace child with reference if possible */
    MVMint32 first_child = MVM_JIT_EXPR_FIRST_CHILD(tree, node);
    MVMint32 child_node = tree->nodes[first_child+child];
    struct Optimizer *o = traverser->data;

    /* double referenced LOAD replacement */
     if (tree->nodes[child_node] == MVM_JIT_LOAD &&
         o->info[child_node].ref_cnt > 1) {
         MVMint32 replacement = MVM_jit_expr_apply_template_adhoc(tc, tree, "ns.", MVM_JIT_COPY, 1, child_node);
         _DEBUG("optimizing multiple (ref_cnt=%d) LOAD (%d) to COPY", o->info[child_node].ref_cnt, child_node);
         replace_node(tc, traverser, tree, child_node, replacement);
    }


    if (o->replacements[child_node] > 0) {
        _DEBUG("Parent node %d assigning replacement node (%d -> %d)",
               node, child_node, o->replacements[child_node]);
        child_node = o->replacements[child_node];
        tree->nodes[first_child+child] = child_node;
        o->replacement_cnt++;
    }

    /* add this parent node as a reference */
    MVM_VECTOR_ENSURE_SPACE(o->refs, 1);
    {
        MVMint32 r = o->refs_num++;

        o->refs[r].next = o->info[child_node].refs;
        o->refs[r].parent = node;
        o->refs[r].ptr    = first_child + child;

        o->info[child_node].refs = r;
        o->info[child_node].ref_cnt++;
    }

}




static void optimize_postorder(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                               MVMJitExprTree *tree, MVMint32 node) {
    /* after postorder, we will not revisit the node, so it's time to replace it */
    struct Optimizer *o = traverser->data;
    MVMint32 replacement = -1;

    switch(tree->nodes[node]) {
    case MVM_JIT_IDX:
    {
        MVMint32 *links = MVM_JIT_EXPR_LINKS(tree, node);
        MVMint32 base = links[0];
        MVMint32 idx  = links[1];
        MVMint32 scale = links[2];
        if (tree->nodes[idx] == MVM_JIT_CONST) {
            MVMint32 ofs = MVM_JIT_EXPR_ARGS(tree, idx)[0] * scale;
            _DEBUG("Const idx (node=%d, base=%d, idx=%d, scale=%d, ofs=%d)\n", node, base, idx, scale, ofs);
            /* insert addr (base, $ofs) */
            replacement = MVM_jit_expr_apply_template_adhoc(tc, tree, "ns..", MVM_JIT_ADDR, 1, base, ofs);
        }
        break;
    }
    case MVM_JIT_ADD:
    {
        MVMJitExprInfo *info = MVM_JIT_EXPR_INFO(tree, node);
        MVMint32 *links = MVM_JIT_EXPR_LINKS(tree, node);
        MVMint32 lhs = links[0];
        MVMint32 rhs = links[1];
        if (tree->nodes[rhs] == MVM_JIT_CONST) {
            MVMint32 cv = MVM_JIT_EXPR_ARGS(tree, rhs)[0];
            if (cv != 0 && info->size == MVM_JIT_PTR_SZ) {
                _DEBUG("Replacing ADD CONST %d to ADDR for pointer-sized addition", cv, cv);
                replacement = MVM_jit_expr_apply_template_adhoc(tc, tree, "ns..", MVM_JIT_ADDR, 1, lhs, cv);
            } else if (cv == 0) {
                replacement = lhs;
            }
        }
        break;
    }
    case MVM_JIT_COPY:
    {
        MVMint32 child = MVM_JIT_EXPR_LINKS(tree, node)[0];
        if (tree->nodes[child] == MVM_JIT_CONST) {
            _DEBUG("Elinimating COPY of CONST (%d)", tree->nodes[child+1]);
            replacement = child;
        }
        break;
    }
    }

    if (replacement > 0) {
        replace_node(tc, traverser, tree, node, replacement);
    }
}


void MVM_jit_expr_tree_optimize(MVMThreadContext *tc, MVMJitGraph *jg, MVMJitExprTree *tree) {
    MVMJitTreeTraverser t;
    struct Optimizer o;
    MVMint32 i;

    /* will nearly always be enough */
    MVM_VECTOR_INIT(o.refs, tree->nodes_num);
    MVM_VECTOR_INIT(o.info, tree->nodes_num * 2);
    MVM_VECTOR_INIT(o.replacements, tree->nodes_num * 2);
    o.replacement_cnt = 0;

    /* Weasly trick: we increment the o->refs_num by one, so that we never
     * allocate zero, so that a zero *refs is the same as 'nothing', so that we
     * don't have to initialize all the refs with -1 in order to indicate a
     * terminator */
    o.refs_num++;

    t.preorder  = optimize_preorder;
    t.inorder   = optimize_child;
    t.postorder = optimize_postorder;
    t.data      = &o;
    t.policy    = MVM_JIT_TRAVERSER_ONCE;
    MVM_jit_expr_tree_traverse(tc, tree, &t);

    MVM_VECTOR_DESTROY(o.refs);
    MVM_VECTOR_DESTROY(o.info);
    MVM_VECTOR_DESTROY(o.replacements);
}
