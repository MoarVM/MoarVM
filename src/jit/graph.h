/* The MVMJitGraph is - for now - really a linked list of instructions.
 * It's likely I'll add complexity when it's needed */
struct MVMJitGraph {
    MVMSpeshGraph *spesh;
    MVMJitIns *first_ins;
    MVMJitIns  *last_ins;

    MVMint32  num_labels;
    MVMJitLabel * labels;
};

/* A label */
struct MVMJitLabel {
    MVMint32 name;
    MVMSpeshBB *bb;
};

struct MVMJitPrimitive {
    MVMSpeshIns * ins;
};

struct MVMJitGuard {
    MVMSpeshIns * ins;
    MVMint32      deopt_target;
    MVMint32      deopt_offset;
};

/* Special branch target for the exit */
#define MVM_JIT_BRANCH_EXIT -1
#define MVM_JIT_BRANCH_OUT  -2

/* What does a branch need? a label to go to, an instruction to read */
struct MVMJitBranch {
    MVMJitLabel dest;
    MVMSpeshIns *ins;
};

typedef enum {
    MVM_JIT_ADDR_STACK,    // relative to stack base (unused)
    MVM_JIT_ADDR_INTERP,   // interpreter variable
    MVM_JIT_ADDR_REG,      // relative to register base
    MVM_JIT_ADDR_REG_F,    // same, but represents a floating point
    MVM_JIT_ADDR_LITERAL,  // constant value
} MVMJitAddrBase;

/* Some interpreter address definition */
#define MVM_JIT_INTERP_TC     0
#define MVM_JIT_INTERP_FRAME  1
#define MVM_JIT_INTERP_CU     2

struct MVMJitAddr {
    MVMJitAddrBase base;
    MVMint32 idx;
};


typedef enum {
    MVM_JIT_RV_VOID,
    /* ptr and int are mostly the same, but they might not be on all
       platforms */
    MVM_JIT_RV_INT, 
    MVM_JIT_RV_PTR,
    /* floats aren't */
    MVM_JIT_RV_NUM,
    /* dereference and store */
    MVM_JIT_RV_DEREF,
    /* store local at address */
    MVM_JIT_RV_ADDR
} MVMJitRVMode;


struct MVMJitCallC {
    void       *func_ptr; 
    MVMJitAddr     *args;   
    MVMuint16   num_args;
    MVMuint16  has_vargs;
    MVMJitRVMode rv_mode;
    MVMint16      rv_idx;
};

/* A non-final list of node types */
typedef enum {
    MVM_JIT_INS_PRIMITIVE,
    MVM_JIT_INS_CALL_C,
    MVM_JIT_INS_BRANCH,
    MVM_JIT_INS_LABEL,
    MVM_JIT_INS_GUARD,
} MVMJitInsType;

struct MVMJitIns {
    MVMJitIns * next;   // linked list
    MVMJitInsType type; // tag
    union {
        MVMJitPrimitive prim;
        MVMJitCallC     call;
        MVMJitBranch    branch;
        MVMJitLabel     label;
        MVMJitGuard     guard;
    } u;
};

MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);
