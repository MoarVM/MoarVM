#include "moar.h"

void MVM_intcache_for(MVMThreadContext *tc, MVMObject *type) {
    int type_index;
    int right_slot = -1;

    fprintf(stderr, "making an intcache for %p\n", type);
    fprintf(stderr, "nursery's at %p and %p right now\n", tc->nursery_fromspace, tc->nursery_tospace);

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
        for (val = 0; val < 16; val++) {
            MVMObject *obj;
            obj = MVM_repr_alloc_init(tc, type);
            MVM_repr_set_int(tc, obj, val);
            tc->instance->int_const_cache->cache[type_index][val] = obj;
            fprintf(stderr, "cached int for type %p number %d is %p\n", type, val, obj);
            MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->int_const_cache->cache[type_index][val]);
        }
        tc->instance->int_const_cache->types[type_index] = type;
        MVM_gc_root_add_permanent(tc, (MVMCollectable **)&tc->instance->int_const_cache->types[type_index]);
    }
    uv_mutex_unlock(&tc->instance->mutex_int_const_cache);
}

MVMObject *MVM_intcache_get(MVMThreadContext *tc, MVMObject *type, MVMint64 value) {
    int type_index;
    int right_slot = -1;

    if (value < 0 || value >= 16)
        return NULL;

    for (type_index = 0; type_index < 4; type_index++) {
        if (tc->instance->int_const_cache->types[type_index] == type) {
            right_slot = type_index;
            break;
        }
    }
    if (right_slot != -1) {
        return tc->instance->int_const_cache->cache[right_slot][value];
    }
    
    fprintf(stderr, "tried to get a cached int for %d of type %p but couldn't :(\n", value, type);
    if (IS_CONCRETE(type)) fprintf(stderr, "oh, it's concrete!\n");

    return NULL;
}
