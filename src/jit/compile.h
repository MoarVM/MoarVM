struct MVMJitCode {
    void     (*func_ptr)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);
    size_t     size;
    MVMuint8  *bytecode;

    MVMStaticFrame *sf;

    MVMuint16 *local_types;
    MVMuint32  num_locals;

    /* The basic idea here is that /all/ label names are indexes into the single
     * labels array. This isn't particularly efficient at runtime (because we
     * need a second dereference to figure the labels out), but very simple for
     * me now, and super-easy to optimise at a later date */
    MVMuint32  num_labels;
    void     **labels;
    void      *exit_label;

    MVMuint32      num_deopts;
    MVMuint32      num_inlines;
    MVMuint32      num_handlers;
    MVMJitDeopt    *deopts;
    MVMJitInline  *inlines;
    MVMJitHandler *handlers;

    MVMuint32      spill_size;
    MVMuint32      seq_nr;

    AO_t ref_cnt;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);

MVMJitCode* MVM_jit_code_copy(MVMThreadContext *tc, MVMJitCode * const code);
void MVM_jit_code_destroy(MVMThreadContext *tc, MVMJitCode *code);

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
void MVM_jit_compile_move(MVMThreadContext *tc, MVMJitCompiler *compiler,
                          MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_memory_copy(MVMThreadContext *tc, MVMJitCompiler *compiler,
                                 MVMJitTile *tile, MVMJitExprTree *tree);
void MVM_jit_compile_guard(MVMThreadContext *tc, MVMJitCompiler *compiler,
                           MVMJitTile *tile, MVMJitExprTree *tree);
