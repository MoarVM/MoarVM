typedef MVMint32 (*MVMJitFunc)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);

struct MVMJitCode {
    MVMJitFunc func_ptr;
    size_t     size;
    MVMuint8  *bytecode;

    MVMStaticFrame *sf;
    /* The basic idea here is that /all/ label names are indexes into
     * the single labels array. This isn't particularly efficient at
     * runtime (because we need a second dereference to figure the
     * labels out), but very simple for me now, and super-easy to
     * optimise at a later date */
    MVMint32   num_labels;
    MVMint32       num_bbs;

    void     **labels;

    MVMint32      *bb_labels;

    MVMint32       num_deopts;
    MVMint32       num_inlines;

    MVMJitDeopt    *deopts;
    MVMJitInline  *inlines;

    MVMint32       num_handlers;
    MVMint32       seq_nr;

    MVMJitHandler *handlers;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);
void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
MVMint32 MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                            MVMJitCode * code);

#define MVM_JIT_CTRL_DEOPT -1
#define MVM_JIT_CTRL_NORMAL 0
