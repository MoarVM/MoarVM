#include "moar.h"

int MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path) {
    MVMDLLRegistry *entry;
    char *cpath;
    DLLib *lib;
    int jen_hash_pad_to_32;
    
    MVMROOT(tc, name, {
        MVMROOT(tc, path, {
            path = MVM_file_in_libpath(tc, path);
        });
    });

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, name);
    jen_hash_pad_to_32 = (name->body.flags & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_UINT8;
    MVM_HASH_GET(tc, tc->instance->dll_registry, name, entry);

    /* already loaded */
    if (entry && entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    cpath = MVM_string_utf8_encode_C_string(tc, path);
    lib = dlLoadLibrary(cpath);

    if (!lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "failed to load library '%s'", cpath);
    }

    free(cpath);

    if (!entry) {
        entry = malloc(sizeof *entry);
        entry->name = name;
        entry->refcount = 0;

        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
        MVM_HASH_BIND(tc, tc->instance->dll_registry, name, entry);
    }

    entry->lib = lib;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);

    return 1;
}

int MVM_dll_free(MVMThreadContext *tc, MVMString *name) {
    MVMDLLRegistry *entry;
    int jen_hash_pad_to_32;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, name);
    jen_hash_pad_to_32 = (name->body.flags & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_UINT8;
    MVM_HASH_GET(tc, tc->instance->dll_registry, name, entry);

    if (!entry) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "cannot free non-existent library");
    }

    /* already freed */
    if (!entry->lib)
        return 0;

    if (entry->refcount > 0) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc(tc, "cannot free in-use library");
    }

    dlFreeLibrary(entry->lib);
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
    int jen_hash_pad_to_32;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, lib);
    jen_hash_pad_to_32 = (lib->body.flags & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_UINT8;
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

    csym = MVM_string_utf8_encode_C_string(tc, sym);
    address = dlFindSymbol(entry->lib, csym);
    free(csym);

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
