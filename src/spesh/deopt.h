void MVM_spesh_deopt_all(MVMThreadContext *tc);
void MVM_spesh_deopt_one(MVMThreadContext *tc, MVMuint32 deopt_idx);
MVMint32 MVM_spesh_deopt_find_inactive_frame_deopt_idx(MVMThreadContext *tc, MVMFrame *f);
void MVM_spesh_deopt_during_unwind(MVMThreadContext *tc);
MVM_STATIC_INLINE MVMuint32 MVM_spesh_deopt_bytecode_pos(MVMuint32 deopt) {
    return deopt >> 1;
}
MVM_STATIC_INLINE MVMuint32 MVM_spesh_deopt_is_pre(MVMuint32 deopt) {
    return deopt & 1;
}
