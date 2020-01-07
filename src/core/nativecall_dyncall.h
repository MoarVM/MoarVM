MVMint16 MVM_nativecall_get_calling_convention(MVMThreadContext *tc, MVMString *name);
#ifdef _WIN32
// Temporary hack until dyncall supports Unicode.
DLLib* MVM_nativecall_load_lib(const char* path);
#else
#define MVM_nativecall_load_lib(path)       dlLoadLibrary(path)
#endif
#define MVM_nativecall_free_lib(lib)        dlFreeLibrary(lib)
#define MVM_nativecall_find_sym(lib, name)  dlFindSymbol(lib, name)
