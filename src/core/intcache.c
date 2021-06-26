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

    /* This order is all rather twisted. We don't want to *write* anything to
     * memory unless offset is non-zero. And we can only figure that out after
     * we've created an object. So we use the cached object for -1 for that.
     * Which means that we then need to twist the loop for (-1 .. 15) around,
     * as we enter it with an object already.
     * And *while* we do that we need to root the type object, just in case,
     * because we can't perma-root it "yet". */
    MVMROOT(tc, type, {
        int val = -1;
        MVMObject *obj = MVM_repr_alloc_init(tc, type);
        MVM_repr_set_int(tc, obj, val);

        MVMuint16 offset;
        if (type_index == MVM_INTCACHE_P6INT_INDEX) {
            offset = offsetof(MVMP6int, body.value);
        } else {
            /* This *can* be zero in nqp-m.
             * If so, we don't want to set up the cache, as we can't use it
             * fully. It seems daft adding defensive code (complication)
             * elsewhere for something which only matters during bootstrapping.
             */
            offset = MVM_p6opaque_get_bigint_offset(tc, obj->st);
        }

        /* effectively here, !offset is an early return. But we can't return
         * early as we have to unwind the MVMROOT and unlock the mutex. */
        if (offset) {
            tc->instance->int_const_cache.stables[type_index] = obj->st;
            /* Yes, clearly we don't *need* to store this given the line above,
             * but doing so saves a pointer indirection (and potential cache
             * miss) in some places, and on 64 bit architectures, alignment
             * means that effectively we can store it for free, in what would
             * have been padding. */
            tc->instance->int_const_cache.sizes[type_index] = obj->st->size;
            tc->instance->int_const_cache.offsets[type_index] = offset;

            while (1) {
                tc->instance->int_const_cache.cache[type_index][val + 1] = obj;
                MVM_gc_root_add_permanent_desc(tc,
                    (MVMCollectable **)&tc->instance->int_const_cache.cache[type_index][val + 1],
                    "Boxed integer cache entry");

                if (++val >= 15) {
                    break;
                }

                obj = MVM_repr_alloc_init(tc, type);
                MVM_repr_set_int(tc, obj, val);
            }

            /* And we are good. (And we "publish" that the cache is valid by
             * setting this pointer non-NULL.) */

            tc->instance->int_const_cache.types[type_index] = type;
            MVM_gc_root_add_permanent_desc(tc,
                (MVMCollectable **)&tc->instance->int_const_cache.types[type_index],
                "Boxed integer cache type");
        }
    });

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
