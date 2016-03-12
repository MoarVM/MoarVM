#include "moar.h"

int MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path) {
    MVMDLLRegistry *entry;
    char *cpath;
    DLLib *lib;

    MVM_string_flatten(tc, name);

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_HASH_GET(tc, tc->instance->dll_registry, name, entry);

    /* already loaded */
    if (entry && entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    MVMROOT(tc, name, {
        MVMROOT(tc, path, {
            path = MVM_file_in_libpath(tc, path);
        });
    });

    cpath = MVM_string_utf8_c8_encode_C_string(tc, path);
    lib = MVM_nativecall_load_lib(cpath);

    if (!lib) {
        char *waste[] = { cpath, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "failed to load library '%s'", cpath);
    }

    MVM_free(cpath);

    if (!entry) {
        entry = MVM_malloc(sizeof *entry);
        entry->name = name;
        entry->refcount = 0;

        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name,
            "DLL name");
        MVM_HASH_BIND(tc, tc->instance->dll_registry, name, entry);
    }

    entry->lib = lib;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);

    return 1;
}

int MVM_dll_free(MVMThreadContext *tc, MVMString *name) {
    MVMDLLRegistry *entry;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->dll_registry, name, entry);

    if (!entry) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "cannot free non-existent library");
    }

    /* already freed */
    if (!entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    if (entry->refcount > 0) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "cannot free in-use library");
    }

    MVM_nativecall_free_lib(entry->lib);
    entry->lib = NULL;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);

    return 1;
}

MVMObject * MVM_dll_find_symbol(MVMThreadContext *tc, MVMString *lib,
        MVMString *sym) {
    MVMDLLRegistry *entry;
    MVMDLLSym *obj;
    char *csym;
    void *address;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, lib);
    MVM_HASH_GET(tc, tc->instance->dll_registry, lib, entry);

    if (!entry) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc,
                "cannot find symbol in non-existent library");
    }

    if (!entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc,
                "cannot find symbol in unloaded library");
    }

    csym = MVM_string_utf8_c8_encode_C_string(tc, sym);
    address = MVM_nativecall_find_sym(entry->lib, csym);
    MVM_free(csym);

    if (!address) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return NULL;
    }

    obj = (MVMDLLSym *)MVM_repr_alloc_init(tc,
            tc->instance->raw_types.RawDLLSym);
    obj->body.address = address;
    obj->body.dll = entry;

    entry->refcount++;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);
    return (MVMObject *)obj;
}

void MVM_dll_drop_symbol(MVMThreadContext *tc, MVMObject *obj) {
    MVMDLLSym *sym;
    MVMDLLRegistry *dll;

    if (REPR(obj)->ID != MVM_REPR_ID_MVMDLLSym)
        MVM_exception_throw_adhoc(tc,
                "unexpected object with REPR other than MVMDLLSym");

    sym = (MVMDLLSym *)obj;
    dll = sym->body.dll;

    if (!dll)
        return;

    MVM_decr(&dll->refcount);

    sym->body.address = NULL;
    sym->body.dll = NULL;
}
