/* Function for getting effective (JIT/specialized/original) bytecode. */
MVM_STATIC_INLINE MVMuint8 * MVM_frame_effective_bytecode(MVMFrame *f) {
    MVMSpeshCandidate *spesh_cand = f->spesh_cand;
    if (spesh_cand)
        return spesh_cand->jitcode ? spesh_cand->jitcode->bytecode : spesh_cand->bytecode;
    return f->static_info->body.bytecode;
}

void MVM_jit_code_enter(MVMThreadContext *tc, MVMJitCode *code, MVMCompUnit *cu);
MVMint32 MVM_jit_code_get_active_handlers(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame, MVMint32 *handlers);
MVMint32 MVM_jit_code_get_active_deopt_idx(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame);
MVMint32 MVM_jit_code_get_active_inlines(MVMThreadContext *tc, MVMJitCode *code, MVMFrame *frame,  MVMint32 *inlines);
