#include "moarvm.h"

MVMHLLConfig *MVM_hll_get_config_for(MVMThreadContext *tc, MVMString *name) {
    void *kdata;
    MVMHLLConfig *entry;
    size_t klen;

    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "get hll config needs concrete string");

    if (apr_thread_mutex_lock(tc->instance->mutex_hllconfigs) != APR_SUCCESS) {
        MVM_exception_throw_adhoc(tc, "Unable to lock hll config hash");
    }

    HASH_FIND(hash_handle, tc->instance->hll_configs, kdata, klen, entry);

    if (!entry) {
        entry = calloc(sizeof(MVMHLLConfig), 1);
        entry->name = name;
        entry->int_box_type = tc->instance->boot_types->BOOTInt;
        entry->num_box_type = tc->instance->boot_types->BOOTNum;
        entry->str_box_type = tc->instance->boot_types->BOOTStr;
        entry->slurpy_array_type = tc->instance->boot_types->BOOTArray;
        entry->slurpy_hash_type = tc->instance->boot_types->BOOTHash;
        HASH_ADD_KEYPTR(hash_handle, tc->instance->hll_configs, kdata, klen, entry);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->int_box_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->num_box_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->str_box_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->slurpy_array_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->slurpy_hash_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->array_iterator_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->hash_iterator_type);
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&entry->name);
    }

    if (apr_thread_mutex_unlock(tc->instance->mutex_hllconfigs) != APR_SUCCESS) {
        MVM_exception_throw_adhoc(tc, "Unable to unlock hll config hash");
    }

    return entry;
}

#define check_config_key(tc, hash, name, member, config) do { \
    MVMObject *key = (MVMObject *)MVM_string_utf8_decode((tc), (tc)->instance->VMString, (name), strlen((name))); \
    MVMObject *val = REPR((hash))->ass_funcs->at_key_boxed((tc), STABLE((hash)), (hash), OBJECT_BODY((hash)), key); \
    if (val) { \
        (config)->member = val; \
    } \
} while (0)

MVMObject * MVM_hll_set_config(MVMThreadContext *tc, MVMString *name, MVMObject *config_hash) {
    MVMHLLConfig *config;

    config = MVM_hll_get_config_for(tc, name);

    if (!config_hash || REPR(config_hash)->ID != MVM_REPR_ID_MVMHash
            || !IS_CONCRETE(config_hash)) {
        MVM_exception_throw_adhoc(tc, "set hll config needs concrete hash");
    }

    check_config_key(tc, config_hash, "int_box", int_box_type, config);
    check_config_key(tc, config_hash, "num_box", num_box_type, config);
    check_config_key(tc, config_hash, "str_box", str_box_type, config);
    check_config_key(tc, config_hash, "slurpy_array", slurpy_array_type, config);
    check_config_key(tc, config_hash, "slurpy_hash", slurpy_hash_type, config);
    check_config_key(tc, config_hash, "array_iterator", array_iterator_type, config);
    check_config_key(tc, config_hash, "hash_iterator", hash_iterator_type, config);

    return config_hash;
}

/* Gets the current HLL configuration. */
MVMHLLConfig *MVM_hll_current(MVMThreadContext *tc) {
    return (*tc->interp_cu)->hll_config;
}
