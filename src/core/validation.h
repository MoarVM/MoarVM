
void MVM_validate_static_frame(MVMThreadContext *tc, MVMStaticFrame *static_frame);
MVMuint32 MVM_bytecode_offset_to_instr_idx(MVMThreadContext *tc,
        MVMStaticFrame *static_frame, MVMuint32 offset);
