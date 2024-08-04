#include "moar.h"

int MVM_dll_load(MVMThreadContext *tc, MVMString *name, MVMString *path) {
    char *cpath;
    DLLib *lib;

    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVMDLLRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->dll_registry, name);

    /* already loaded */
    if (entry && entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    MVMROOT2(tc, name, path) {
        path = MVM_file_in_libpath(tc, path);
    }

    cpath = MVM_string_utf8_c8_encode_C_string(tc, path);
    lib = MVM_nativecall_load_lib(cpath);

    if (!lib) {
        char *waste[] = { cpath, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "failed to load library '%s'", cpath);
    }

    MVM_free(cpath);

    if (!entry) {
        entry = MVM_fixkey_hash_insert_nocheck(tc, &tc->instance->dll_registry, name);
        entry->refcount = 0;
        MVM_gc_root_add_permanent_desc(tc, (MVMCollectable **)&entry->hash_key,
            "DLL name hash key");
    }

    entry->lib = lib;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);

    return 1;
}

int MVM_dll_free(MVMThreadContext *tc, MVMString *name) {
    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVMDLLRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->dll_registry, name);

    if (!entry) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "cannot free non-existent library '%s'", c_name);
    }

    /* already freed */
    if (!entry->lib) {
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        return 0;
    }

    if (entry->refcount > 0) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste, "cannot free in-use library '%s'", c_name);
    }

    MVM_nativecall_free_lib(entry->lib);
    entry->lib = NULL;

    uv_mutex_unlock(&tc->instance->mutex_dll_registry);

    return 1;
}

MVMObject * MVM_dll_find_symbol(MVMThreadContext *tc, MVMString *lib,
        MVMString *sym) {
    MVMDLLSym *obj;
    char *csym;
    void *address;

    if (!MVM_str_hash_key_is_valid(tc, lib)) {
        MVM_str_hash_key_throw_invalid(tc, lib);
    }

    uv_mutex_lock(&tc->instance->mutex_dll_registry);

    MVMDLLRegistry *entry = MVM_fixkey_hash_fetch_nocheck(tc, &tc->instance->dll_registry, lib);

    if (!entry) {
        char *c_lib = MVM_string_utf8_encode_C_string(tc, lib);
        char *waste[] = { c_lib, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste,
                "cannot find symbol '%s' in non-existent library", c_lib);
    }

    if (!entry->lib) {
        char *c_lib = MVM_string_utf8_encode_C_string(tc, lib);
        char *waste[] = { c_lib, NULL };
        uv_mutex_unlock(&tc->instance->mutex_dll_registry);
        MVM_exception_throw_adhoc_free(tc, waste,
                "cannot find symbol '%s' in unloaded library", c_lib);
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
