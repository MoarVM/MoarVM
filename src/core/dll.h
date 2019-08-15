struct MVMDLLRegistry {
    DLLib *lib;
    MVMString *name;
    AO_t refcount;
    UT_hash_handle hash_handle;
};

typedef struct MVMDLLRegistry MVMDLL;

MVMDLL * MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path);
void MVM_dll_retain(MVMThreadContext *tc, MVMDLL *dll);
int MVM_dll_release(MVMThreadContext *tc, MVMDLL *dll);
MVMDLL * MVM_dll_get(MVMThreadContext *tc, MVMString *name);
void * MVM_dll_find_symbol(MVMThreadContext *tc, MVMDLL *dll, MVMString *sym);
MVMObject * MVM_dll_box(MVMThreadContext *tc, MVMDLL *dll, MVMObject *type);
MVMDLL * MVM_dll_unbox(MVMThreadContext *tc, MVMObject *obj);
