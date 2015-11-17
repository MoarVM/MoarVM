/* Each node that yields a value has a type. This information can
 * probably be used by the code generator, somehow. */
typedef enum { /* value type */
     MVM_JIT_VOID,
     MVM_JIT_REG,
     MVM_JIT_FLAG,
     MVM_JIT_INT,
     MVM_JIT_NUM,
     MVM_JIT_PTR,
} MVMJitExprVtype;

#define MVM_JIT_PTR_SZ sizeof(void*)
#define MVM_JIT_REG_SZ sizeof(MVMRegister)
#define MVM_JIT_INT_SZ sizeof(MVMint64)
#define MVM_JIT_NUM_SZ sizeof(MVMnum64)


/* Control casting behaviour for mixed-sized operands */
#define MVM_JIT_NO_CAST  0
#define MVM_JIT_UNSIGNED 1
#define MVM_JIT_SIGNED   2

/* This defines a macro that defines a list which will use a macro to
   define a list. It's a little trick I've gained from the luajit
   source code - the big advantage of course is that it keeps the list
   consistent across multiple definitions.

   The first argument is the name, the second the number of children,
   the third the number of parameters - together they define the size
   of the node. The fourth argument defines the result type which I
   vaguely presume to be useful in code generation (NB: It isn't; it
   can go). The fifth argument determines how to generate a cast for
   mixed-sized oeprands. */

#define MVM_JIT_IR_OPS(_) \
    /* memory access */ \
    _(LOAD, 1, 1, REG, NO_CAST),   \
    _(STORE, 2, 1, VOID, NO_CAST), \
    _(CONST, 0, 2, REG, NO_CAST),  \
    _(ADDR, 1, 1, REG, UNSIGNED),  \
    _(IDX, 2, 1, REG, UNSIGNED),   \
    _(COPY, 1, 0, REG, NO_CAST),   \
    /* type conversion */ \
    _(CAST, 1, 2, REG, NO_CAST),   \
    /* integer comparison */ \
    _(LT, 2, 0, FLAG, SIGNED),     \
    _(LE, 2, 0, FLAG, SIGNED),     \
    _(EQ, 2, 0, FLAG, SIGNED),     \
    _(NE, 2, 0, FLAG, SIGNED),     \
    _(GE, 2, 0, FLAG, SIGNED),     \
    _(GT, 2, 0, FLAG, SIGNED),     \
    _(NZ, 1, 0, FLAG, UNSIGNED),   \
    _(ZR, 1, 0, FLAG, UNSIGNED),   \
    /* flag value */ \
    _(FLAGVAL, 1, 0, REG, NO_CAST), \
    /* integer arithmetic */ \
    _(ADD, 2, 0, REG, SIGNED), \
    _(SUB, 2, 0, REG, SIGNED), \
    /* binary operations */ \
    _(AND, 2, 0, REG, UNSIGNED), \
    _(OR, 2, 0, REG, UNSIGNED),  \
    _(XOR, 2, 0, REG, UNSIGNED), \
    _(NOT, 1, 0, REG, UNSIGNED), \
    /* boolean logic */ \
    _(ALL, -1, 0, FLAG, NO_CAST), \
    _(ANY, -1, 0, FLAG, NO_CAST), \
    /* control operators */ \
    _(DO, -1, 0, REG, NO_CAST),   \
    _(WHEN, 2, 0, VOID, NO_CAST), \
    _(IF, 3, 0, REG, NO_CAST),    \
    _(EITHER, 3, 0, VOID, NO_CAST), \
    _(BRANCH, 1, 0, VOID, NO_CAST), \
    _(LABEL, 1, 0, VOID, NO_CAST),  \
    /* special control operators */ \
     _(INVOKISH, 1, 0, VOID, NO_CAST), \
     _(THROWISH, 1, 0, VOID, NO_CAST), \
    /* call c functions */ \
    _(CALL, 2, 1, REG, NO_CAST),      \
    _(ARGLIST, -1, 0, VOID, NO_CAST), \
    _(CARG, 1, 1, VOID, NO_CAST),     \
    /* interpreter special variables */ \
    _(TC, 0, 0, REG, NO_CAST), \
    _(CU, 0, 0, REG, NO_CAST), \
    _(FRAME, 0, 0, REG, NO_CAST), \
    _(LOCAL, 0, 0, REG, NO_CAST), \
    _(STACK, 0, 0, REG, NO_CAST), \
    _(VMNULL, 0, 0, REG, NO_CAST), \
    /* End of list marker */ \
    _(MAX_NODES, 0, 0, VOID, NO_CAST), \



enum MVMJitExprOp {
#define MVM_JIT_IR_ENUM(name, nchild, npar, vtype, cast) MVM_JIT_##name
MVM_JIT_IR_OPS(MVM_JIT_IR_ENUM)
#undef MVM_JIT_IR_ENUM
};

typedef MVMint64 MVMJitExprNode;

struct MVMJitExprOpInfo {
    const char     *name;
    MVMint32        nchild;
    MVMint32        nargs;
    MVMJitExprVtype vtype;
    MVMint8         cast;
};

struct MVMJitExprValue {
    /* used to signal register allocator, tiles don't look at this */
    MVMJitExprVtype type;
    enum {
        MVM_JIT_VALUE_EMPTY,
        MVM_JIT_VALUE_ALLOCATED,
        MVM_JIT_VALUE_SPILLED,
        MVM_JIT_VALUE_DEAD,
        MVM_JIT_VALUE_IMMORTAL
    } state;

    /* register allocated to this value */
    MVMint8 reg_cls;
    MVMint8 reg_num;

    /* Spill location if any */
    MVMint16 spill_location;

    /* size of this value */
    MVMint8  size;

    /* Use information for register allcoator */
    MVMint32 first_created;
    MVMint32 last_created;
    MVMint32 last_use;
    MVMint32 num_use;
};

/* Tree node information for easy access and use during compilation (a
   symbol table entry of sorts) */
struct MVMJitExprNodeInfo {
    const MVMJitExprOpInfo *op_info;
    /* VM instruction represented by this node */
    MVMSpeshIns    *spesh_ins;
    /* VM Local value of this node */
    MVMint16        local_addr;

    /* Tiler result */
    const MVMJitTile *tile;
    MVMint32          tile_state;
    MVMint32          tile_rule;

    /* internal label for IF/WHEN/ALL/ANY etc, relative to the tree label offset */
    MVMint32          label;

    /* Result value information (register/memory location, size etc) */
    MVMJitExprValue value;
};

struct MVMJitExprTree {
    MVMJitGraph *graph;
    MVM_DYNAR_DECL(MVMJitExprNode, nodes);
    MVM_DYNAR_DECL(MVMint32, roots);
    MVM_DYNAR_DECL(MVMJitExprNodeInfo, info);

    MVMint32 label_ofs;
    MVMint32 num_labels;
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

    MVM_DYNAR_DECL(MVMint32, visits);
    enum {
        MVM_JIT_TRAVERSER_REPEAT,
        MVM_JIT_TRAVERSER_ONCE
    } policy;

};


const MVMJitExprOpInfo * MVM_jit_expr_op_info(MVMThreadContext *tc, MVMJitExprNode node);
MVMJitExprTree * MVM_jit_expr_tree_build(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshIterator *iter);
void MVM_jit_expr_tree_traverse(MVMThreadContext *tc, MVMJitExprTree *tree, MVMJitTreeTraverser *traverser);
void MVM_jit_expr_tree_destroy(MVMThreadContext *tc, MVMJitExprTree *tree);
