
/* Each node that yields a value has a type. This information can
 * probably be used by the code generator, somehow. */
enum MVMJitExprVtype { /* value type */
     MVM_JIT_VOID,
     MVM_JIT_REG,
     MVM_JIT_MEM,
     MVM_JIT_FLAG,
};

enum MVMJitAddrBase {
    MVM_JIT_FARGS, /* frame arg */
    MVM_JIT_LOCAL, /* local arg */
};

#define MVM_JIT_PTR_SZ sizeof(void*)
#define funcptr(x) ((uintptr_t)(x))

/* This defines a macro that defines a list which will use a macro to
   define a list. It's a little trick I've gained from the luajit
   source code - the big advantage of course is that it keeps the list
   consistent. */
#define MVM_JIT_IR_OPS(_) \
    /* memory access */ \
    _(LOAD, 2, REG), \
    _(STORE, 3, VOID), \
    _(CONST, 2, REG), \
    _(ADDR, 2, MEM), \
    _(COPY, 1, REG), \
    /* integer comparison */ \
    _(LT, 2, FLAG), \
    _(LE, 2, FLAG), \
    _(EQ, 2, FLAG), \
    _(GE, 2, FLAG), \
    _(GT, 2, FLAG), \
    _(NZ, 1, FLAG), \
    _(ZR, 1, FLAG), \
    /* integer arithmetic */ \
    _(ADD, 2, REG), \
    /* boolean logic */ \
    _(FLAGS, 1, REG), \
    _(NOT, 1, REG),  \
    _(AND, 2, REG), \
    _(OR, 2, REG), \
    _(IF, 2, VOID), \
    _(IFELSE, 3, REG), \
    /* call c functions */ \
    _(CALL, 4, REG), \
    _(ARGLIST, -1, VOID), \
    _(CARG, 2, VOID), \
    /* interpreter special variables */ \
    _(TC, 0, REG), \
    _(VMNULL, 0, REG),




enum MVMJitExprOp {
#define MVM_JIT_IR_ENUM(name, nargs, vtype) MVM_JIT_##name
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

MVMJitExprTree * MVM_jit_build_expression_tree(MVMThreadContext *tc, MVMJitGraph *jg, MVMSpeshBB *bb);
