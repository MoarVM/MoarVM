/* The MVMJitGraph is - for now - really a linked list of instructions.
 * It's likely I'll add complexity when it's needed */
struct MVMJitGraph {
    MVMSpeshGraph *sg;
    MVMJitNode    *first_node;
    MVMJitNode    *last_node;

    /* total number of labels, including deopt labels, handler labels,
       and inline start-stops labels */
    MVMint32       num_labels;
    /* bb labels are all basic block label numbers, indexed by basic
       block number */
    MVMint32       num_bbs;
    MVMint32      *bb_labels;
    
    MVMint32       num_deopts;
    MVMJitDeopt   *deopts;

    MVMint32       num_handlers;
    MVMJitHandler *handlers;
    
    MVMint32       num_inlines;
    MVMJitInline  *inlines;
};

struct MVMJitDeopt {
    MVMint32 idx;
    MVMint32 label;
};

struct MVMJitHandler {
    MVMint32 start_label;
    MVMint32 end_label;
    MVMint32 goto_label;
};

struct MVMJitInline {
    MVMint32 start_label;
    MVMint32 end_label;
};

/* A label (no more than a number) */
struct MVMJitLabel {
    MVMint32    name;
};

struct MVMJitPrimitive {
    MVMSpeshIns * ins;
};

struct MVMJitGuard {
    MVMSpeshIns * ins;
    MVMint32      deopt_target;
    MVMint32      deopt_offset;
};


#define MVM_JIT_INFO_INVOKISH 1
#define MVM_JIT_INFO_THROWISH 2

typedef enum {
    MVM_JIT_CONTROL_INVOKISH,
    MVM_JIT_CONTROL_DYNAMIC_LABEL,
    MVM_JIT_CONTROL_THROWISH_PRE,
    MVM_JIT_CONTROL_THROWISH_POST,
    MVM_JIT_CONTROL_BREAKPOINT,
} MVMJitControlType;

struct MVMJitControl {
    MVMSpeshIns       *ins;
    MVMJitControlType type;
};

/* Special branch target for the exit */
#define MVM_JIT_BRANCH_EXIT -1
#define MVM_JIT_BRANCH_OUT  -2

/* What does a branch need? a label to go to, an instruction to read */
struct MVMJitBranch {
    MVMint32     dest;
    MVMSpeshIns *ins;
};

typedef enum {
    MVM_JIT_INTERP_TC,
    MVM_JIT_INTERP_CU,
    MVM_JIT_INTERP_FRAME,
    MVM_JIT_INTERP_PARAMS,
    MVM_JIT_INTERP_CALLER,
} MVMJitInterpVar;

typedef enum {
    MVM_JIT_INTERP_VAR,
    MVM_JIT_REG_VAL,
    MVM_JIT_REG_VAL_F,
    MVM_JIT_REG_ADDR,
    MVM_JIT_STR_IDX,
    MVM_JIT_LITERAL,
    MVM_JIT_LITERAL_F,
    MVM_JIT_LITERAL_64,
    MVM_JIT_LITERAL_PTR,
    MVM_JIT_REG_STABLE,
    MVM_JIT_REG_OBJBODY,
} MVMJitArgType;

struct MVMJitCallArg {
    MVMJitArgType type;
    union {
        MVMint64      lit_i64;
        MVMnum64      lit_n64;
        MVMJitInterpVar  ivar;
        MVMint16          reg;
        void             *ptr;
    } v;
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
    MVMJitCallArg  *args;
    MVMuint16   num_args;
    MVMuint16  has_vargs;
    MVMJitRVMode rv_mode;
    MVMint16      rv_idx;
};

struct MVMJitInvoke {
    MVMint16      callsite_idx;
    MVMint16      arg_count;
    MVMSpeshIns **arg_ins;
    MVMReturnType return_type;
    MVMint16      return_register;
    MVMint16      code_register;
    MVMint16      spesh_cand;
    MVMint8       is_fast;
    MVMint32      reentry_label;
};

struct MVMJitJumpList {
    MVMint64 num_labels;
    MVMint16 reg;
    // labels of the goto's / jump instructions themselves
    MVMint32 *in_labels;
    // labels the goto's jump to
    MVMint32 *out_labels;
};

/* Node types */
typedef enum {
    MVM_JIT_NODE_PRIMITIVE,
    MVM_JIT_NODE_CALL_C,
    MVM_JIT_NODE_BRANCH,
    MVM_JIT_NODE_LABEL,
    MVM_JIT_NODE_GUARD,
    MVM_JIT_NODE_INVOKE,
    MVM_JIT_NODE_JUMPLIST,
    MVM_JIT_NODE_CONTROL,
} MVMJitNodeType;

struct MVMJitNode {
    MVMJitNode   * next; // linked list
    MVMJitNodeType type; // tag
    union {
        MVMJitPrimitive prim;
        MVMJitCallC     call;
        MVMJitBranch    branch;
        MVMJitLabel     label;
        MVMJitGuard     guard;
        MVMJitInvoke    invoke;
        MVMJitJumpList  jumplist;
        MVMJitControl   control;
    } u;
};

MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *sg);
