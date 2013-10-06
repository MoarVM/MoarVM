typedef void MVMExtOpFunc(MVMThreadContext *tc);

struct MVMExtRegistry {
    MVMDLLSym *sym;
    MVMString *name;
    UT_hash_handle hash_handle;
};

struct MVMExtOpRegistry {
    MVMOpInfo info;
    MVMExtOpFunc *func;
    MVMString *name;
    UT_hash_handle hash_handle;
};

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext);
int MVM_ext_register_extop(MVMThreadContext *tc, const char *cname,
        MVMExtOpFunc func, MVMuint8 num_operands, MVMuint8 operands[]);
