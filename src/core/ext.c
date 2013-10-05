#include "moar.h"

int MVM_ext_load(MVMThreadContext *tc, MVMString *lib, MVMString *ext) {
    MVMString *colon = MVM_string_ascii_decode_nt(
            tc, tc->instance->VMString, ":");
    MVMString *prefix = MVM_string_concatenate(tc, lib, colon);
    MVMString *name = MVM_string_concatenate(tc, prefix, ext);
    MVMExtRegistry *entry;
    MVMDLLSym *sym;
    void (*init)(MVMThreadContext *);

    uv_mutex_lock(&tc->instance->mutex_ext_registry);

    MVM_string_flatten(tc, name);
    MVM_HASH_GET(tc, tc->instance->ext_registry, name, entry);

    if (entry) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        return 0;
    }

    sym = (MVMDLLSym *)MVM_dll_find_symbol(tc, lib, ext);
    if (!sym) {
        uv_mutex_unlock(&tc->instance->mutex_ext_registry);
        MVM_exception_throw_adhoc(tc, "extension symbol not found");
    }

    entry = malloc(sizeof *entry);
    entry->sym = sym;
    entry->name = name;

    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
    MVM_HASH_BIND(tc, tc->instance->ext_registry, name, entry);

    uv_mutex_unlock(&tc->instance->mutex_ext_registry);

    /* Call extension's initializer */
    init = (void (*)(MVMThreadContext *))sym->body.address;
    init(tc);

    return 1;
}
