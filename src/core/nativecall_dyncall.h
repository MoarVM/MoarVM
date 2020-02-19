MVMint16 MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name);
#define MVM_nativecall_load_lib(path)       dlLoadLibrary(path)
#define MVM_nativecall_free_lib(lib)        dlFreeLibrary(lib)
#define MVM_nativecall_find_sym(lib, name)  dlFindSymbol(lib, name)
