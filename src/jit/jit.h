struct MVMJitGraph {
    MVMSpeshGraph *spesh;
};
struct MVMJitNode {

};

struct MVMJitCCall {
    struct MVMJitNode node;
    void * func_ptr;
    MVMuint16 num_args;
    MVMuint16 has_vargs;
};

/* Function pointer for invoking a JIT code segment. I might change this, of course */
typedef void (*MVMJitFunc)(MVMThreadContext *tc, MVMCallsite * callsite, 
			   MVMRegister * args);;

MVMJitGraph* MVM_jit_try_make_jit_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);


