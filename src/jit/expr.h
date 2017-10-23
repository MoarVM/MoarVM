/* Each node that yields a value has a type. This information can
 * probably be used by the code generator, somehow. */
typedef enum { /* value type */
     MVM_JIT_VOID,
     MVM_JIT_REG,
     MVM_JIT_FLAG,
     MVM_JIT_INT,
     MVM_JIT_NUM,
     MVM_JIT_PTR,
     MVM_JIT_C_ARGS,
} MVMJitExprVtype;

#define MVM_JIT_PTR_SZ sizeof(void*)
#define MVM_JIT_REG_SZ sizeof(MVMRegister)
#define MVM_JIT_INT_SZ sizeof(MVMint64)
#define MVM_JIT_NUM_SZ sizeof(MVMnum64)


/* Control casting behaviour for mixed-sized operands */
#define MVM_JIT_NO_CAST  0
#define MVM_JIT_UNSIGNED 1
#define MVM_JIT_SIGNED   2


#include "expr_ops.h"


enum {
#define MVM_JIT_OP_ENUM(name, nchild, npar, vtype, cast) MVM_JIT_##name
MVM_JIT_EXPR_OPS(MVM_JIT_OP_ENUM)
#undef MVM_JIT_OP_ENUM
};

typedef MVMint64 MVMJitExprNode;

struct MVMJitExprOpInfo {
    const char     *name;
    MVMint32        nchild;
    MVMint32        nargs;
    MVMJitExprVtype vtype;
    MVMint8         cast;
};

/* Tree node information for easy access and use during compilation (a
   symbol table entry of sorts) */
struct MVMJitExprNodeInfo {
    const MVMJitExprOpInfo *op_info;
    /* VM instruction represented by this node */
    MVMSpeshIns    *spesh_ins;
    /* VM 'register' type represented by this node */
    MVMint8          opr_type;
    /* Size of computed value */
    MVMint8         size;
    /* internal label for IF/WHEN/ALL/ANY etc, relative to the tree label offset */
    MVMint32        label;
};

struct MVMJitExprTree {
    MVMJitGraph *graph;
    MVM_VECTOR_DECL(MVMJitExprNode, nodes);
    MVM_VECTOR_DECL(MVMint32, roots);
    MVM_VECTOR_DECL(MVMJitExprNodeInfo, info);

    MVMint32 label_ofs;
    MVMint32 num_labels;
    MVMuint32 seq_nr;
};

struct MVMJitExprTemplate {
    const MVMJitExprNode *code;
    const char *info;
    MVMint32 len;
    MVMint32 root;
    MVMint32 flags;
};

#define MVM_JIT_EXPR_TEMPLATE_VALUE       0
#define MVM_JIT_EXPR_TEMPLATE_DESTRUCTIVE 1


struct MVMJitTreeTraverser {
    void  (*preorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node);
    void   (*inorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node, MVMint32 child);
    void (*postorder)(MVMThreadContext *tc, MVMJitTreeTraverser *traverser,
                      MVMJitExprTree *tree, MVMint32 node);
    void       *data;

    MVM_VECTOR_DECL(MVMint32, visits);
    enum {
        MVM_JIT_TRAVERSER_REPEAT,
        MVM_JIT_TRAVERSER_ONCE
    } policy;

};


const MVMJitExprOpInfo * MVM_jit_expr_op_info(MVMThreadContext *tc, MVMint32 op);
/* properties of expression ops */
MVMint32 MVM_jit_expr_op_negate_flag(MVMThreadContext *tc, MVMint32 op);
MVMint32 MVM_jit_expr_op_is_binary_noncommutative(MVMThreadContext *tc, MVMint32 op);

MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIterator *iter);
MVMint32 MVM_jit_expr_apply_template(MVMThreadContext *tc, MVMJitExprTree *tree, const MVMJitExprTemplate*, MVMint32 *operands);
MVMint32 MVM_jit_expr_apply_template_adhoc(MVMThreadContext *tc, MVMJitExprTree *tree, char *template, ...);
void MVM_jit_expr_tree_traverse(MVMThreadContext *tc, MVMJitExprTree *tree, MVMJitTreeTraverser *traverser);
void MVM_jit_expr_tree_destroy(MVMThreadContext *tc, MVMJitExprTree *tree);
MVMint32 MVM_jit_expr_tree_get_nodes(MVMThreadContext *tc, MVMJitExprTree *tree,
                                     MVMint32 node, const char *path, MVMJitExprNode *buffer);
