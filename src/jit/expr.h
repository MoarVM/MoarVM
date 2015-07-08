
/* Each node that yields a value has a type. This information can
 * probably be used by the code generator, somehow. */
enum MVMJitExprVtype { /* value type */
     MVM_JIT_VOID,
     MVM_JIT_REG,
     MVM_JIT_MEM,
     MVM_JIT_FLAG,
};

#define MVM_JIT_PTR_SZ sizeof(void*)
#define MVM_JIT_REG_SZ sizeof(MVMRegister)
#define funcptr(x) ((uintptr_t)(x))

/* This defines a macro that defines a list which will use a macro to
   define a list. It's a little trick I've gained from the luajit
   source code - the big advantage of course is that it keeps the list
   consistent across multiple definitions.

   The first argument is the name, the second the number of children,
   the third the number of parameters - together they define the size
   of the node. The last argument defines the result type - which I
   vaguely presume to be useful in code generation. */

#define MVM_JIT_IR_OPS(_) \
    /* memory access */ \
    _(LOAD, 1, 1, REG), \
    _(STORE, 2, 1, VOID), \
    _(CONST, 0, 2, REG), \
    _(ADDR, 1, 1, MEM), \
    _(IDX, 2, 1, MEM), \
    _(COPY, 1, 0, REG), \
    /* integer comparison */ \
    _(LT, 2, 0, FLAG), \
    _(LE, 2, 0, FLAG), \
    _(EQ, 2, 0, FLAG), \
    _(GE, 2, 0, FLAG), \
    _(GT, 2, 0, FLAG), \
    _(NZ, 1, 0, FLAG), \
    _(ZR, 1, 0, FLAG), \
    /* integer arithmetic */ \
    _(ADD, 2, 0, REG), \
    _(SUB, 2, 0, REG), \
    /* boolean logic */ \
    _(FLAGS, 1, 0, REG), \
    _(NOT, 1, 0, REG),  \
    _(AND, 2, 0, REG), \
    _(OR, 2, 0, REG), \
    _(IF, 2, 0, VOID), \
    _(IFELSE, 3, 0, REG), \
    /* call c functions */ \
    _(CALL, 2, 1, REG), \
    _(ARGLIST, -1, 0, VOID), \
    _(CARG, 1, 1, VOID), \
    /* interpreter special variables */ \
    _(TC, 0, 0, REG), \
    _(CU, 0, 0, MEM), \
    _(FRAME, 0, 0, MEM), \
    _(LOCAL, 0, 0, MEM), \
    _(VMNULL, 0, 0, REG), \
    /* End of list marker */ \
    _(MAX_NODES, 0, 0, VOID),\



enum MVMJitExprOp {
#define MVM_JIT_IR_ENUM(name, nchild, npar, vtype) MVM_JIT_##name
MVM_JIT_IR_OPS(MVM_JIT_IR_ENUM)
#undef MVM_JIT_IR_ENUM
};


typedef MVMint64 MVMJitExprNode;

typedef struct {
    MVMJitExprNode *nodes;
    MVMint32 *roots;
    MVMint32 num_nodes;
    MVMint32 num_roots;
}  MVMJitExprTree;

typedef struct {
    const MVMJitExprNode *code;
    const char *info;
    MVMint32 len;
    MVMint32 root;
} MVMJitExprTemplate;

/* not sure what this will look like yet */
typedef struct {
    void (*visit)(MVMThreadContext *tc, MVMJitExprTree *tre, void *data,
                  MVMint32 position, MVMint32 direction);
    void *data;
} MVMJitTreeTraverser;


MVMJitExprTree * MVM_jit_build_expression_tree(MVMThreadContext *tc, MVMSpeshGraph *sg,
                                               MVMSpeshBB *bb);
#define MVM_JIT_TREE_DOWN 0
#define MVM_JIT_TREE_UP 1

void MVM_jit_traverse_tree(MVMThreadContext *tc, MVMJitExprTree *tree,
                           MVMJitTreeTraverser *traverser);
void MVM_jit_dump_tree(MVMThreadContext *tc, MVMJitExprTree *tree);
