
char * MVM_bytecode_dump(MVMThreadContext *tc, MVMCompUnit *cu);

#ifdef DEBUG_HELPERS
void MVM_dump_bytecode_of_starting_at(MVMThreadContext *tc, MVMFrame *frame, MVMSpeshCandidate *maybe_candidate, MVMuint32 starting_offset, MVMint32 show_lines);
void MVM_dump_bytecode_of(MVMThreadContext *tc, MVMFrame *frame, MVMSpeshCandidate *maybe_candidate);
void MVM_dump_bytecode_staticframe(MVMThreadContext *tc, MVMStaticFrame *frame);
void MVM_dump_bytecode_starting_at(MVMThreadContext *tc, MVMint32 start_offset, MVMint32 show_lines);
void MVM_dump_bytecode_near_ip(MVMThreadContext *tc);
void MVM_dump_bytecode(MVMThreadContext *tc);
void MVM_dump_bytecode_stackframe(MVMThreadContext *tc, MVMint32 depth);
#endif
