struct MVMJitGraph {
    MVMSpeshGraph * spesh;
    MVMJitIns * first_ins;
    MVMJitIns * last_ins;
    MVMint32 num_labels;
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
    MVM_JIT_ARG_STACK,  // relative to stack base
    MVM_JIT_ARG_REG,    // relative to register base
    MVM_JIT_ARG_CONST,  // constant value
} MVMJitArgBase;

/* Stack-frame offsets for variables */
#define MVM_JIT_STACK_TC    (sizeof(MVMThreadContext*))
#define MVM_JIT_STACK_FRAME (sizeof(MVMThreadContext*) + sizeof(MVMFrame*))

struct MVMJitCallArg {
    MVMJitArgBase base;
    MVMuint64 offset;
};

struct MVMJitCallC {
    void * func_ptr; // what do we call
    MVMJitCallArg * args; // a list of arguments
    MVMuint16 num_args; // how many arguments we pass
    MVMuint16 has_vargs; // does the receiver consider them variable
};

typedef enum {
    MVM_JIT_INS_PRIMITIVE,
    MVM_JIT_INS_CALL_C,
    MVM_JIT_INS_BRANCH,
} MVMJitInsType;

struct MVMJitIns {
    MVMJitIns * next;   // linked list
    MVMJitInsType type; // tag
    MVMint32 label;     // dynamic label
    union {
        MVMJitPrimitive prim;
        MVMJitCallC     call;
        MVMJitBranch    branch;
    } u;
};


MVMJitGraph* MVM_jit_try_make_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);
MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph, size_t *codesize_out);
MVMuint8* MVM_jit_magic_bytecode(MVMThreadContext *tc, MVMuint32 *bytecode_size_out);

