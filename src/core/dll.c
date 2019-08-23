#include "moar.h"

#ifdef _WIN32
#include <windows.h>
extern char __ImageBase[];
#endif

MVMObject * MVM_dll_box(MVMThreadContext *tc, MVMDLL *dll, MVMObject *type)
{
    MVMObject *obj;
    MVMint64 val;

    if (IS_CONCRETE(type)) {
        MVM_exception_throw_adhoc(tc, "cannot box into concrete object");
    }

    if (REPR(type)->ID != MVM_REPR_ID_MVMCPointer) {
        MVM_exception_throw_adhoc(tc,
                "box type must have representation CPointer");
    }

    obj = MVM_repr_alloc_init(tc, type);
    ((MVMCPointer *)obj)->body.ptr = dll;
    return obj;
}

MVMDLL * MVM_dll_unbox(MVMThreadContext *tc, MVMObject *obj)
{
    if (REPR(obj)->ID != MVM_REPR_ID_MVMCPointer) {
        MVM_exception_throw_adhoc(tc,
                "box type must have representation CPointer");
    }

    if (!IS_CONCRETE(obj)) {
        MVM_exception_throw_adhoc(tc, "cannot unbox type object");
    }

    return ((MVMCPointer *)obj)->body.ptr;
}

void MVM_dll_retain(MVMThreadContext *tc, MVMDLL *dll)
{
    AO_t old_count = MVM_incr(&dll->refcount);

    /* oops... */
    if (old_count == 0) {
        MVM_decr(&dll->refcount); /* can this race hurt us? */
        MVM_exception_throw_adhoc(tc, "cannot retain unloaded dll");
    }
}

int MVM_dll_release(MVMThreadContext *tc, MVMDLL *dll)
{
    AO_t old_count = MVM_decr(&dll->refcount);
    if (old_count > 1) return 0;

    /* oops... */
    if (old_count == 0) {
        MVM_incr(&dll->refcount); /* can this race hurt us?
                                     AO_t is unsigned, so no 'bad' overflow */

        MVM_exception_throw_adhoc(tc, "cannot release unloaded dll");
    }

    /* old_count was 1, so unload the dll */
    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    /* check for race: if someone's retained the lib in the meantime,
       don't unload */
    if (dll->refcount > 0) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    MVM_nativecall_free_lib(dll->lib);
    dll->lib = NULL;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);
    return 1;
}

MVMDLL * MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path)
{
    MVMDLL *dll;
    char *cpath;
    DLLib *lib;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_HASH_GET(tc, tc->instance->dll_registry, name, dll);

    /* already loaded */
    if (dll && dll->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return dll;
    }

    MVMROOT2(tc, name, path, {
        path = MVM_file_in_libpath(tc, path);
    });

    cpath = MVM_string_utf8_c8_encode_C_string(tc, path);
    lib = MVM_nativecall_load_lib(cpath);

    if (!lib) {
        char *waste[] = { cpath, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste,
                "failed to load library '%s'", cpath);
    }

    MVM_free(cpath);

    if (!dll) {
        dll = MVM_malloc(sizeof *dll);
        dll->name = name;
        dll->refcount = 1;

        MVM_gc_root_add_permanent_desc(tc,
                (MVMCollectable **)&dll->name, "DLL name");
        MVM_HASH_BIND(tc, tc->instance->dll_registry, name, dll);
        MVM_gc_root_add_permanent_desc(tc,
                (MVMCollectable **)&dll->hash_handle.key, "DLL name hash key");
    }

    dll->lib = lib;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);
    return dll;
}

MVMDLL * MVM_dll_get(MVMThreadContext *tc, MVMString *name)
{
    MVMDLL *dll;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_HASH_GET(tc, tc->instance->dll_registry, name, dll);
    if(!dll || !dll->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "dll not loaded");
    }

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);
    return dll;
}

void * MVM_dll_find_symbol(MVMThreadContext *tc, MVMDLL *dll, MVMString *sym)
{
    /* no mutex: the refcounting should protect us */
    char *csym = MVM_string_utf8_c8_encode_C_string(tc, sym);
    void *ptr;

    if(dll) ptr = MVM_nativecall_find_sym(dll->lib, csym);
    else {
#ifdef _WIN32
        /* search in current module */
        ptr = (void *)GetProcAddress(GetModuleHandle(NULL), csym);

        /* search in MoarVM's module */
        if(!ptr) ptr = (void *)GetProcAddress((HMODULE)__ImageBase, csym);

        /* search in libc */
        if(!ptr) {
            /* or: GetProcAddress(ReverseGetModuleHandle(SOME_LIBC_FUNC)) */
            const void *some_address = stderr;
            HMODULE module;

            if(GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    some_address, &module)) {
                ptr = (void *)GetProcAddress(module, csym);
            }
        }
#else
        DLLib *lib = MVM_nativecall_load_lib(NULL);
        ptr = MVM_nativecall_find_sym(lib, csym);
        MVM_nativecall_free_lib(lib);
#endif
    }

    MVM_free(csym);
    return ptr;
}
