typedef void (*MVMJitFunc)(MVMThreadContext *tc, MVMCompUnit *cu, void * label);

struct MVMJitCode {
    MVMJitFunc func_ptr;
    size_t     size;
    MVMuint8  *bytecode;

    MVMStaticFrame *sf;
    /* The basic igdea here is that /all/ label names are indexes into
     * the single labels array. This isn't particularly efficient at
     * runtime (because we need a second dereference to figure the
     * labels out), but very simple for me now, and super-easy to
     * optimise at a later date */
    MVMint32   num_labels;
    void     **labels;

    MVMint32       num_deopts;
    MVMint32       num_inlines;
    MVMJitDeopt    *deopts;
    MVMJitInline  *inlines;

    MVMint32       num_handlers; /* for handlers */
    MVMint32       seq_nr;
    MVMJitHandler *handlers;
};

MVMJitCode* MVM_jit_compile_graph(MVMThreadContext *tc, MVMJitGraph *graph);
void MVM_jit_destroy_code(MVMThreadContext *tc, MVMJitCode *code);
void MVM_jit_enter_code(MVMThreadContext *tc, MVMCompUnit *cu,
                        MVMJitCode * code);

/* Function for getting effective (JIT/specialized/original) bytecode. */
MVM_STATIC_INLINE MVMuint8 * MVM_frame_effective_bytecode(MVMFrame *f) {
    MVMSpeshCandidate *spesh_cand = f->spesh_cand;
    if (spesh_cand)
        return spesh_cand->jitcode ? spesh_cand->jitcode->bytecode : spesh_cand->bytecode;
    return f->static_info->body.bytecode;
}
