void MVM_spesh_deopt_all(MVMThreadContext *tc);
void MVM_spesh_deopt_one(MVMThreadContext *tc);
void MVM_spesh_deopt_one_direct(MVMThreadContext *tc, MVMint32 deopt_offset,
                                MVMint32 deopt_target);
