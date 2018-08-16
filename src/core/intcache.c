#include "moar.h"

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type) {
    int type_index;
    int right_slot = -1;
    uv_mutex_lock(&tc->instance->mutex_int_const_cache);
    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == NULL) {
            right_slot = type_index;
            break;
        }
        else if (tc->instance->int_const_cache->types[type_index] == type) {
            uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
            return;
        }
    }
    if (right_slot != -1) {
        int val;
        for (val = -1; val < 15; val++) {
            MVMObject *obj;
            obj = MVM_repr_alloc_init(tc, type);
            MVM_repr_set_int(tc, obj, val);
            tc->instance->int_const_cache->cache[type_index][val + 1] = obj;
            MVM_gc_root_add_permanent_desc(tc,
                (MVMCollectable **)&tc->instance->int_const_cache->cache[type_index][val + 1],
                "Boxed integer cache entry");
        }
        tc->instance->int_const_cache->types[type_index] = type;
        MVM_gc_root_add_permanent_desc(tc,
            (MVMCollectable **)&tc->instance->int_const_cache->types[type_index],
            "Boxed integer cache type");
    }
    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
}

MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    int type_index;
    int right_slot = -1;

    if (value < -1 || value >= 15)
        return NULL;

    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == type) {
            right_slot = type_index;
            break;
        }
    }
    if (right_slot != -1) {
        return tc->instance->int_const_cache->cache[right_slot][value + 1];
    }
    return NULL;
}

MVMint32 MVM_intcache_type_index(MVMThreadContext *tc, MVMObject *type) {
    int type_index;
    int found = -1;
    uv_mutex_lock(&tc->instance->mutex_int_const_cache);
    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == type) {
            found = type_index;
            break;
        }
    }
    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
    return found;
}
