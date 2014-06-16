struct MVMJitGraph {
    MVMSpeshGraph * spesh;
    MVMJitIns * first_ins;
    MVMJitIns * last_ins;
    MVMint32 num_labels;
};


struct MVMJitLabel {
    MVMJitIns *ins;
    MVMSpeshBB *bb;
    MVMint32 label;
};

struct MVMJitPrimitive {
    MVMSpeshIns * ins;
};



/* Special branch target for the exit */
#define MVM_JIT_BRANCH_EXIT -1

struct MVMJitBranch {
    MVMint32 destination;
};

typedef enum {
    MVM_JIT_ADDR_STACK,    // relative to stack base
    MVM_JIT_ADDR_INTERP,   // interpreter variable
    MVM_JIT_ADDR_REG,      // relative to register base
    MVM_JIT_ADDR_LITERAL,  // constant value
} MVMJitAddrBase;

/* Some interpreter address definition */
#define MVM_JIT_INTERP_TC     0
#define MVM_JIT_INTERP_FRAME  1

struct MVMJitAddr {
    MVMJitAddrBase base;
    MVMint32 idx;
};

struct MVMJitCallC {
    void * func_ptr;     // what do we call
    MVMJitAddr * args;   // a list of arguments
    MVMuint16 num_args;  // how many arguments we pass
    MVMuint16 has_vargs; // does the receiver consider them variable
};


/* A non-final list of node types */
typedef enum {
    MVM_JIT_INS_PRIMITIVE,
    MVM_JIT_INS_CALL_C,
    MVM_JIT_INS_BRANCH,
} MVMJitInsType;

struct MVMJitIns {
    MVMJitIns * next;   // linked list
    MVMJitInsType type; // tag
    union {
        MVMJitPrimitive prim;
        MVMJitCallC     call;
        MVMJitBranch    branch;
        MVMJitLabel     label;
    } u;
};



MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);
MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph, size_t *codesize_out);
MVMuint8* MVM_jit_magic_bytecode(MVMThreadContext *tc, MVMuint32 *bytecode_size_out);
void MVM_enter_jit(MVMThreadContext *tc, MVMFrame *frame, MVMJitCode jitcode);
