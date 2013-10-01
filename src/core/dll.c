#include "moarvm.h"
#include <dynload.h>

int MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path) {
    MVMDLLRegistry *entry;
    char *cpath;
    DLLib *lib;

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, name);
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

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVM_string_flatten(tc, name);
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
