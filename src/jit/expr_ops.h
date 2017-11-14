/* This defines a macro that defines a list which will use a macro to
   define a list. It's a little trick I've gained from the luajit
   source code - the big advantage of course is that it keeps the list
   consistent across multiple definitions.

   The first argument is the name, the second the number of children, the third
   the number of parameters - together they define the size of the node. The
   fourth argument defines the result type. This is strictly redundant for code
   generation, although it is used by the template compiler. The fifth argument
   determines how to generate a cast for mixed-sized oeprands.

   NB: This file is parsed by tools/expr_ops.pm *AND* included by
   src/jit/expr.h, so keep it in order!
*/
#define MVM_JIT_EXPR_OPS(_) \
    /* invalid operator */ \
    _(NOOP, 0, 0, VOID, NO_CAST), \
    /* wrap value of other operator */ \
    _(COPY, 1, 0, REG, NO_CAST),   \
    /* memory access */ \
    _(LOAD, 1, 1, REG, NO_CAST),   \
    _(STORE, 2, 1, VOID, NO_CAST), \
    _(ADDR, 1, 1, REG, UNSIGNED),  \
    _(IDX, 2, 1, REG, UNSIGNED),   \
    /* constant up to 4 bytes */ \
    _(CONST, 0, 2, REG, NO_CAST),  \
    /* constant pointer (architecture-dependent size) */ \
    _(CONST_PTR, 0, 1, REG, NO_CAST), \
    /* large constant (8 bytes) */ \
    _(CONST_LARGE, 0, 2, REG, NO_CAST), \
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
    /* force compilation but discard result */ \
    _(DISCARD, 1, 0, VOID, NO_CAST),       \
    /* type conversion */ \
    _(CAST, 1, 3, REG, NO_CAST),   \
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
    _(DOV, -1, 0, VOID, NO_CAST),   \
    _(WHEN, 2, 0, VOID, NO_CAST), \
    _(IF, 3, 0, REG, NO_CAST),    \
    _(IFV, 3, 0, VOID, NO_CAST), \
    _(BRANCH, 1, 0, VOID, NO_CAST), \
    _(LABEL, 0, 1, REG, NO_CAST),  \
    _(MARK, 0, 1, VOID, NO_CAST), \
    /* call c functions */ \
    _(CALL, 2, 1, REG, NO_CAST),      \
    _(CALLV, 2, 0, VOID, NO_CAST), \
    _(ARGLIST, -1, 0, C_ARGS, NO_CAST), \
    _(CARG, 1, 1, VOID, NO_CAST),     \
    /* special constrol structures */ \
    _(GUARD, 1, 2, VOID, NO_CAST),  \
    /* interpreter special variables */ \
    _(TC, 0, 0, REG, NO_CAST), \
    _(CU, 0, 0, REG, NO_CAST), \
    _(LOCAL, 0, 0, REG, NO_CAST), \
    _(STACK, 0, 0, REG, NO_CAST), \
    /* End of list marker */ \
    _(MAX_NODES, 0, 0, VOID, NO_CAST)
