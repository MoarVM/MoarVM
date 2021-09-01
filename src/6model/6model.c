#include "moar.h"

/* Gets the HOW (meta-object), which may be lazily deserialized. */
MVMObject * MVM_6model_get_how(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *HOW = st->HOW;
    if (!HOW && st->HOW_sc)
        MVM_ASSIGN_REF(tc, &(st->header), st->HOW, HOW = MVM_sc_get_object(tc, st->HOW_sc, st->HOW_idx));
    return HOW ? HOW : tc->instance->VMNull;
}

/* Gets the HOW (meta-object), which may be lazily deserialized, through the
 * STable of the passed object. */
MVMObject * MVM_6model_get_how_obj(MVMThreadContext *tc, MVMObject *obj) {
    return MVM_6model_get_how(tc, STABLE(obj));
}

/* Tries to do a type check using the cache. If the type is in the cache, then
 * result will be set to a true value and a true value will be returned. If it
 * is not in the cache and the cache is authoritative, then we know the answer
 * too; result is set to zero and a true value is returned. Otherwise, we can
 * not tell and a false value is returned and result is undefined. */
MVMint64 MVM_6model_try_cache_type_check(MVMThreadContext *tc, MVMObject *obj,
        MVMObject *type, MVMint64 *result) {
    /* A null is always false. */
    if (MVM_is_null(tc, obj)) {
        *result = 0;
        return 1;
    }

    /* Consider type cache. */
    MVMuint16 i, elems = STABLE(obj)->type_check_cache_length;
    MVMObject  **cache = STABLE(obj)->type_check_cache;
    if (cache) {
        for (i = 0; i < elems; i++) {
            if (cache[i] == type) {
                *result = 1;
                return 1;
            }
        }
        if ((STABLE(obj)->mode_flags & MVM_TYPE_CHECK_CACHE_THEN_METHOD) == 0 &&
            (STABLE(type)->mode_flags & MVM_TYPE_CHECK_NEEDS_ACCEPTS) == 0) {
            *result = 0;
            return 1;
        }
    }
    return 0;
}

/* Clean up STable memory. */
void MVM_6model_stable_gc_free(MVMThreadContext *tc, MVMSTable *st) {
    /* First have it free its repr_data if it wants. */
    if (st->REPR->gc_free_repr_data)
        st->REPR->gc_free_repr_data(tc, st);

    /* free various storage. */
    MVM_free(st->type_check_cache);
    if (st->container_spec && st->container_spec->gc_free_data)
        st->container_spec->gc_free_data(tc, st);
    MVM_free(st->boolification_spec);
    MVM_free(st->debug_name);
}

/* Get the next type cache ID for a newly created STable. */
MVMuint64 MVM_6model_next_type_cache_id(MVMThreadContext *tc) {
    return (MVMuint64)MVM_add(&tc->instance->cur_type_cache_id, MVM_TYPE_CACHE_ID_INCR) + MVM_TYPE_CACHE_ID_INCR;
}

/* For type objects, marks the type as never repossessable. For concrete object
 * instances, marks the individual ojbect as never repossessable. */
void MVM_6model_never_repossess(MVMThreadContext *tc, MVMObject *obj) {
    if (IS_CONCRETE(obj))
        obj->header.flags1 |= MVM_CF_NEVER_REPOSSESS;
    else
        obj->st->mode_flags |= MVM_NEVER_REPOSSESS_TYPE;
}

/* Set the debug name on a type. */
void MVM_6model_set_debug_name(MVMThreadContext *tc, MVMObject *type, MVMString *name) {
    char *orig_debug_name;
    uv_mutex_lock(&(tc->instance->mutex_free_at_safepoint));
    orig_debug_name = STABLE(type)->debug_name;
    if (orig_debug_name)
        MVM_free_at_safepoint(tc, orig_debug_name);
    STABLE(type)->debug_name = name && MVM_string_graphs(tc, name)
        ? MVM_string_utf8_encode_C_string(tc, name)
        : NULL;
    uv_mutex_unlock(&(tc->instance->mutex_free_at_safepoint));
}
