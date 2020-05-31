struct MVMDLLRegistry {
    DLLib *lib;
    AO_t refcount;
    UT_hash_handle hash_handle;
};

int MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path);
int MVM_dll_free(MVMThreadContext *tc, MVMString *name);
MVMObject * MVM_dll_find_symbol(MVMThreadContext *tc, MVMString *lib, MVMString *sym);
void MVM_dll_drop_symbol(MVMThreadContext *tc, MVMObject *obj);
