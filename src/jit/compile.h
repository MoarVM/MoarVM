
struct MVMJitCode {
    void     (*func_ptr)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);
    size_t     size;
    MVMuint8  *bytecode;

    MVMStaticFrame *sf;
    /* The basic idea here is that /all/ label names are indexes into
     * the single labels array. This isn't particularly efficient at
     * runtime (because we need a second dereference to figure the
     * labels out), but very simple for me now, and super-easy to
     * optimise at a later date */
    MVMint32   num_labels;
    void     **labels;

    MVMint32       num_deopts;
    MVMJitDeopt    *deopts;

    MVMint32       num_inlines;
    MVMJitInline  *inlines;

    MVMint32       num_handlers;
    MVMJitHandler *handlers;


    MVMint32       spill_size;

    MVMint32       seq_nr;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);

void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode * code);

/* Peseudotile compile functions */
void MVM_jit_compile_label(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                            MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_conditional_branch(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                        MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_store(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_load(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_breakpoint(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                MVMJitTile *tile, MVMJitExprTree *tree);
