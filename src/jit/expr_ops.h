/* This defines a macro that defines a list which will use a macro to
   define a list. It's a little trick I've gained from the luajit
   source code - the big advantage of course is that it keeps the list
   consistent across multiple definitions.

   The first argument is the name, the second the number of children, the third
   the number of parameters - together they define the size of the node.

   NB: This file is parsed by tools/expr_ops.pm *AND* included by
   src/jit/expr.h, so keep it in order!
*/
#define MVM_JIT_EXPR_OPS(_) \
    /* invalid operator */ \
    _(NOOP, 0, 0), \
    /* wrap value of other operator */ \
    _(COPY, 1, 0),   \
    /* memory access */ \
    _(LOAD, 1, 1),   \
    _(LOAD_NUM, 1, 1), \
    _(STORE, 2, 1), \
    _(STORE_NUM, 2, 1), \
    _(ADDR, 1, 1),  \
    _(IDX, 2, 1),   \
    /* constant up to 4 bytes */ \
    _(CONST, 0, 2),  \
    /* constant pointer (architecture-dependent size) */ \
    _(CONST_PTR, 0, 1), \
    /* large constant (8 bytes) */ \
    _(CONST_LARGE, 0, 2), \
    /* floating point constant */ \
    _(CONST_NUM, 0, 2), \
    /* integer comparison */ \
    _(LT, 2, 0),     \
    _(LE, 2, 0),     \
    _(EQ, 2, 0),     \
    _(NE, 2, 0),     \
    _(GE, 2, 0),     \
    _(GT, 2, 0),     \
    _(NZ, 1, 0),   \
    _(ZR, 1, 0),   \
    /* flag value */ \
    _(FLAGVAL, 1, 0), \
    /* force compilation but discard result */ \
    _(DISCARD, 1, 0),       \
    /* type conversion */ \
    _(SCAST, 1, 2),   \
    _(UCAST, 1, 2),   \
    /* integer arithmetic */ \
    _(ADD, 2, 0), \
    _(SUB, 2, 0), \
    _(MUL, 2, 0), \
    /* binary operations */ \
    _(AND, 2, 0), \
    _(OR, 2, 0),  \
    _(XOR, 2, 0), \
    _(NOT, 1, 0), \
    /* boolean logic */ \
    _(ALL, -1, 0), \
    _(ANY, -1, 0), \
    /* control operators */ \
    _(DO, -1, 0),   \
    _(DOV, -1, 0),   \
    _(WHEN, 2, 0), \
    _(IF, 3, 0),    \
    _(IFV, 3, 0), \
    _(BRANCH, 1, 0), \
    _(LABEL, 0, 1),  \
    _(MARK, 0, 1), \
    /* call c functions */ \
    _(CALL, 2, 1),      \
    _(CALLV, 2, 0), \
    _(ARGLIST, -1, 0), \
    _(CARG, 1, 1),     \
    /* special constrol structures */ \
    _(GUARD, 1, 2),  \
    /* interpreter special variables */ \
    _(TC, 0, 0), \
    _(CU, 0, 0), \
    _(LOCAL, 0, 0), \
    _(STACK, 0, 0), \
    /* End of list marker */ \
    _(MAX_NODES, 0, 0)
