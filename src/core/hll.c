#include "moarvm.h"

MVMHLLConfig *MVM_hll_get_config_for(MVMThreadContext *tc, MVMString *name) {
    void *kdata;
    MVMHLLConfig *entry;
    size_t klen;
    
    MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, name, "bad String");
    
    if (apr_thread_mutex_lock(tc->instance->mutex_hllconfigs) != APR_SUCCESS) {
        MVM_exception_throw_adhoc(tc, "Unable to lock hll config hash");
    }
    
    HASH_FIND(hash_handle, tc->instance->hll_configs, kdata, klen, entry);
    
    if (!entry) {
        entry = calloc(sizeof(MVMHLLConfig), 1);
        entry->name = name;
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
    
    if (apr_thread_mutex_unlock(tc->instance->mutex_hllconfigs) != APR_SUCCESS)
        MVM_exception_throw_adhoc(tc, "Unable to unlock hll config hash");
    
    return entry;
}
