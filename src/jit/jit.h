/* Whats the basic idea here? For now, an MVMJitGraph is simply a
 * linear stream of compilable instructions. The difference between
 * this and MVMSpeshGraph is that it contains information to help 
 * allocate registers and select instructions. 
 *
 * This isn't even my final form.
 */
struct MVMJitGraph {
    MVMSpeshGraph * spesh;
    MVMJitNode * entry;
    MVMJitNode * exit;
};


struct MVMJitNode {
    MVMJitNode * next;
};


struct MVMJitCallC {
    MVMJitNode node;
    void * func_ptr; // what do we call
    MVMuint16 num_args; // how many arguments we pass
    MVMuint16 has_vargs; // does the receiver consider them variable
};


typedef void (*MVMJitCode)(MVMThreadContext *tc, MVMFrame *frame);

MVMJitGraph* MVM_jit_try_make_jit_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh);
MVMJitCode MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);


