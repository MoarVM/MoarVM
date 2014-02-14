#include "moar.h"

/* XXX adding new types to the cache should be protected by a mutex */

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type) {
    int type_index;
    int right_slot = -1;
    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == NULL) {
            right_slot = type_index;
            break;
        }
        else if (tc->instance->int_const_cache->types[type_index] == type) {
            return;
        }
    }
    if (right_slot != -1) {
        int val;
        for (val = 0; val < 16; val++) {
            MVMObject *obj;
            obj = MVM_repr_alloc_init(tc, type);
            MVM_repr_set_int(tc, obj, val);
            tc->instance->int_const_cache->cache[type_index][val] = obj;
            MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->int_const_cache->cache[type_index][val]);
        }
        tc->instance->int_const_cache->types[type_index] = type;
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->int_const_cache->types[type_index]);
    }
}

MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    int type_index;
    int right_slot = -1;
    MVMObject *result;

    if (value < 0 || value >= 16)
        return NULL;

    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == type) {
            right_slot = type_index;
            break;
        }
    }
    if (right_slot != -1) {
        MVMint64 res_val;
        result = tc->instance->int_const_cache->cache[right_slot][value];
        res_val = MVM_repr_get_int(tc, result);
        if (res_val != value) {
            printf("the num is %ld, expected %ld\n", res_val, value);
        }
        return result;
    }
    return NULL;
}
