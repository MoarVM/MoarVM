#include "moar.h"

#if defined(MVM_JIT_DEBUG) && MVM_JIT_DEBUG == MVM_JIT_DEBUG_OPTIMIZE
#define _DEBUG(fmt, ...) do { fprintf(stderr, fmt "%s", __VA_ARGS__, "\n"); } while(0)
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
};

static void optimize_preorder(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                              MVMJitExprTree *tree, MVMint32 node) {
    /* i can imagine that there might be interesting optimizations we could apply here */

}

static void optimize_child(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                           MVMJitExprTree *tree, MVMint32 node, MVMint32 child) {
    /* add reference from parent to child, replace child with reference if possible */
    MVMint32 first_child = MVM_JIT_EXPR_FIRST_CHILD(tree, node);
    MVMint32 child_node = tree->nodes[first_child+child];
    struct Optimizer *o = traverser->data;
    if (o->replacements[child_node] > 0) {
        tree->nodes[first_child+child] = o->replacements[child_node];
        child_node = o->replacements[child_node];
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

/* TODO - figure out a more general way to do it (it is kind of template-like,  I think */
MVMint32 wrap_copy(MVMThreadContext *tc, struct Optimizer *o, MVMJitExprTree *tree, MVMint32 node) {
    MVMint32 root = MVM_jit_expr_apply_template_adhoc(tc, tree, "ns.", MVM_JIT_COPY, 1, node);
    MVM_VECTOR_ENSURE_SIZE(o->info, root);
    return root;
}

static void optimize_postorder(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                               MVMJitExprTree *tree, MVMint32 node) {
    /* after postorder, we will not revisit the node, so it's time to replace it */
    struct Optimizer *o = traverser->data;
    MVMint32 replacement = -1;

    switch(tree->nodes[node]) {
    case MVM_JIT_LOAD:
        if (o->info[node].ref_cnt > 1) {
            _DEBUG("Replacing a double-referenced load with a copy\n");
            replacement = wrap_copy(tc, o, tree, node);
        }
        break;
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
    }

    if (replacement > 0) {
        /* double pointer iteration to update all children */
        MVMint32 *c;
        _DEBUG("Replaced node %d with %d\n", node, replacement);

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
