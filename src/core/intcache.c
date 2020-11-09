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
        MVMObject *obj;
        MVMROOT(tc, type, {
            int val;
            for (val = -1; val < 15; val++) {
                obj = MVM_repr_alloc_init(tc, type);
                MVM_repr_set_int(tc, obj, val);
                tc->instance->int_const_cache->cache[type_index][val + 1] = obj;
                MVM_gc_root_add_permanent_desc(tc,
                    (MVMCollectable **)&tc->instance->int_const_cache->cache[type_index][val + 1],
                    "Boxed integer cache entry");
            }
        });
        tc->instance->int_const_cache->types[type_index] = type;
        tc->instance->int_const_cache->stables[type_index] = obj->st;
        if (REPR(type)->ID == MVM_REPR_ID_P6int) {
            tc->instance->int_const_cache->offsets[type_index] = offsetof(MVMP6int, body.value);
        }
        else if (REPR(type)->ID == MVM_REPR_ID_P6opaque) {
            tc->instance->int_const_cache->offsets[type_index]
                = MVM_p6opaque_get_bigint_offset(tc, obj->st);
        }

        MVM_gc_root_add_permanent_desc(tc,
            (MVMCollectable **)&tc->instance->int_const_cache->types[type_index],
            "Boxed integer cache type");
    }
    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
}
