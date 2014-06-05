struct MVMJitGraph {
    MVMSpeshGraph * spesh;
    MVMJitNode * entry;
    MVMJitNode * exit;
};

typedef enum {
    MVM_JIT_NODE_BRANCH,
    MVM_JIT_NODE_ARITH,
    MVM_JIT_NODE_LOAD,
    MVM_JIT_NODE_STORE,
    MVM_JIT_NODE_VALUE,
    MVM_JIT_NODE_ENTRY,
    MVM_JIT_NODE_EXIT,
} MVMJitNodeType;

struct MVMJitNode {
    MVMJitNodeType type;
    MVMJitNode * next;
};

typedef enum {
    MVM_JIT_VALUE_I8,
    MVM_JIT_VALUE_I16,
    MVM_JIT_VALUE_I32,
    MVM_JIT_VALUE_I64,
    MVM_JIT_VALUE_N32,
    MVM_JIT_VALUE_N64,
    MVM_JIT_VALUE_STR,
    MVM_JIT_VALUE_OBJ
} MVMJitValueType;

struct MVMJitValue {
    MVMJitValueType type;
    union {
	MVMuint8   u8;
	MVMuint16 u16;
	MVMuint32 u32;
	MVMuint64 u64;
	MVMint8    i8;
	MVMint16  i16;
	MVMint32  i32;
	MVMint64  i64;
	MVMnum32  n32;
	MVMnum64  n64;
	MVMObject * o;
	MVMString * s;
    } u;
};


struct MVMJitStore {
    MVMJitNode node;
    MVMJitValue value;
};

typedef enum {
    MVM_JIT_BRANCH_JUMP,
    MVM_JIT_BRANCH_COND,
    MVM_JIT_BRANCH_CALL,
    MVM_JIT_BRANCH_RETURN
} MVMJitBranchType;

struct MVMJitBranch {
    MVMJitNode node;
    MVMJitBranchType type;
    MVMuint32 label;
};

struct MVMJitCCall {
    MVMJitNode node;
    void * func_ptr; // what do we call
    MVMuint16 num_args; // how many arguments we pass
    MVMuint16 has_vargs; // if so, we need to declare that number in 
};



typedef void (*MVMJitCode)(MVMThreadContext *tc, MVMRegister * base,
			   MVMCallsite * callsite,  MVMRegister * args);;

MVMJitGraph* MVM_jit_try_make_jit_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);
MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);


