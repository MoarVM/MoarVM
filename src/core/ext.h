struct MVMExtRegistry {
    MVMDLLSym *sym;
    MVMString *name;
    UT_hash_handle hash_handle;
};

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext);
#if 0
void MVM_ext_register_extop(MVMThreadContext *tc, MVMExtOp *op);
#endif
