#include "moar.h"

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type) {
    int type_index = MVM_intcache_possible_type_index(type);
    if (type_index < 0) {
        return;
    }

    uv_mutex_lock(&tc->instance->mutex_int_const_cache);

    MVMObject *was = tc->instance->int_const_cache.types[type_index];
    if (was == type) {
        uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
        return;
    }

    if (was) {
        uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
        MVM_oops(tc, "Duplicate intcache initialisation for %d - %p vs %p",
                 type_index, was, type);
        return;
    }

    MVMROOT(tc, type, {
        int val;
        for (val = -1; val < 15; val++) {
            MVMObject *obj;
            obj = MVM_repr_alloc_init(tc, type);
            MVM_repr_set_int(tc, obj, val);
            tc->instance->int_const_cache.cache[type_index][val + 1] = obj;
            MVM_gc_root_add_permanent_desc(tc,
                (MVMCollectable **)&tc->instance->int_const_cache.cache[type_index][val + 1],
                "Boxed integer cache entry");
        }
    });

    tc->instance->int_const_cache.types[type_index] = type;
    MVM_gc_root_add_permanent_desc(tc,
        (MVMCollectable **)&tc->instance->int_const_cache.types[type_index],
        "Boxed integer cache type");

    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
}

MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    if (!(MVM_INTCACHE_RANGE_CHECK(value))) {
        return NULL;
    }

    int type_index = MVM_intcache_possible_type_index(type);
    if (type_index < 0) {
        return NULL;
    }

    if (tc->instance->int_const_cache.types[type_index] != type) {
        return NULL;
    }

    return tc->instance->int_const_cache.cache[type_index][value + MVM_INTCACHE_ZERO_OFFSET];
}

int MVM_intcache_type_index(MVMThreadContext *tc, MVMObject *type) {
    int type_index = MVM_intcache_possible_type_index(type);
    if (type_index < 0) {
        return type_index;
    }

    int found = -1;
    uv_mutex_lock(&tc->instance->mutex_int_const_cache);
    if (tc->instance->int_const_cache.types[type_index] == type) {
        found = type_index;
    }
    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
    return found;
}
