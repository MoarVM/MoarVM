void MVM_load_bytecode_buffer(MVMThreadContext *tc, MVMObject *buf);
void MVM_load_bytecode_buffer_to_cu(MVMThreadContext *tc, MVMObject *buf, MVMRegister *res);
void MVM_load_bytecode(MVMThreadContext *tc, MVMString *filename);
void MVM_load_bytecode_fh(MVMThreadContext *tc, MVMObject *oshandle, MVMString *filename);
