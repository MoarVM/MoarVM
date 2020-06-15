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

/* Obtains the method cache, lazily deserializing if it needed. */
static MVMObject * get_method_cache(MVMThreadContext *tc, MVMSTable *st) {
    if (!st->method_cache)
        MVM_serialization_finish_deserialize_method_cache(tc, st);
    return st->method_cache;
}

/* Locates a method by name, checking in the method cache only. */
MVMObject * MVM_6model_find_method_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache;

    MVMROOT(tc, name, {
        cache = get_method_cache(tc, STABLE(obj));
    });

    if (cache && IS_CONCRETE(cache))
        return MVM_repr_at_key_o(tc, cache, name);
    return NULL;
}

/* Locates a method by name. Returns the method if it exists, or throws an
 * exception if it can not be found. */
typedef struct {
    MVMObject   *obj;
    MVMString   *name;
    MVMRegister *res;
    MVMint64     throw_if_not_found;
} FindMethodSRData;
static void die_over_missing_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *handler = MVM_hll_current(tc)->method_not_found_error;
    if (!MVM_is_null(tc, handler)) {
        MVMCallsite *methnotfound_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_STR);
        handler = MVM_frame_find_invokee(tc, handler, NULL);
        MVM_args_setup_thunk(tc, NULL, MVM_RETURN_VOID, methnotfound_callsite);
        tc->cur_frame->args[0].o = obj;
        tc->cur_frame->args[1].s = name;
        STABLE(handler)->invoke(tc, handler, methnotfound_callsite, tc->cur_frame->args);
        return;
    }
    else {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Cannot find method '%s' on object of type %s",
            c_name, MVM_6model_get_debug_name(tc, obj));
    }
}
static void find_method_unwind(MVMThreadContext *tc, void *sr_data) {
    MVM_free(sr_data);
}
static void late_bound_find_method_return(MVMThreadContext *tc, void *sr_data) {
    FindMethodSRData *fm = (FindMethodSRData *)sr_data;
    if (MVM_is_null(tc, fm->res->o) || !IS_CONCRETE(fm->res->o)) {
        if (fm->throw_if_not_found) {
            MVMObject *obj  = fm->obj;
            MVMString *name = fm->name;
            MVM_free(fm);
            die_over_missing_method(tc, obj, name);
        }
        else {
            fm->res->o = tc->instance->VMNull;
            MVM_free(fm);
        }
    }
    else {
        MVM_free(fm);
    }
}
static void mark_find_method_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    FindMethodSRData *fm = (FindMethodSRData *)frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &fm->obj);
    MVM_gc_worklist_add(tc, worklist, &fm->name);
}
void MVM_6model_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name,
                            MVMRegister *res, MVMint64 throw_if_not_found) {
    MVMObject *cache = NULL, *HOW = NULL, *find_method = NULL, *code = NULL;
    MVMCallsite *findmeth_callsite = NULL;

    if (MVM_is_null(tc, obj)) {
        if (throw_if_not_found) {
            char *c_name  = MVM_string_utf8_encode_C_string(tc, name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                "Cannot call method '%s' on a null object",
                 c_name);
        }
        else {
            res->o = tc->instance->VMNull;
            return;
        }
    }

    /* First try to find it in the cache. If we find it, we have a result.
     * If we don't find it, but the cache is authoritative, then error. */
    MVMROOT2(tc, obj, name, {
        cache = get_method_cache(tc, STABLE(obj));
    });

    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = MVM_repr_at_key_o(tc, cache, name);
        if (!MVM_is_null(tc, meth)) {
            res->o = meth;
            return;
        }
        if (STABLE(obj)->mode_flags & MVM_METHOD_CACHE_AUTHORITATIVE) {
            if (throw_if_not_found)
                die_over_missing_method(tc, obj, name);
            else
                res->o = tc->instance->VMNull;
            return;
        }
    }

    /* Otherwise, need to call the find_method method. We make the assumption
     * that the invocant's meta-object's type is composed. */
    MVMROOT3(tc, obj, name, HOW, {
       HOW = MVM_6model_get_how(tc, STABLE(obj));
       find_method = MVM_6model_find_method_cache_only(tc, HOW,
            tc->instance->str_consts.find_method);
    });

    if (MVM_is_null(tc, find_method)) {
        if (throw_if_not_found) {
            char *c_name  = MVM_string_utf8_encode_C_string(tc, name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                "Cannot find method '%s' on '%s': no method cache and no .^find_method",
                 c_name, MVM_6model_get_debug_name(tc, obj));
        }
        else {
            res->o = tc->instance->VMNull;
            return;
        }
    }

    /* Set up the call, using the result register as the target. */
    code = MVM_frame_find_invokee(tc, find_method, NULL);
    findmeth_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ_STR);
    MVM_args_setup_thunk(tc, res, MVM_RETURN_OBJ, findmeth_callsite);
    {
        FindMethodSRData *fm = MVM_malloc(sizeof(FindMethodSRData));
        fm->obj  = obj;
        fm->name = name;
        fm->res  = res;
        fm->throw_if_not_found = throw_if_not_found;
        MVM_frame_special_return(tc, tc->cur_frame, late_bound_find_method_return,
            find_method_unwind, fm, mark_find_method_sr_data);
    }
    tc->cur_frame->args[0].o = HOW;
    tc->cur_frame->args[1].o = obj;
    tc->cur_frame->args[2].s = name;
    STABLE(code)->invoke(tc, code, findmeth_callsite, tc->cur_frame->args);
}

MVMint32 MVM_6model_find_method_spesh(MVMThreadContext *tc, MVMObject *obj, MVMString *name,
                                      MVMint32 ss_idx, MVMRegister *res) {
    MVMObject *meth;

    /* Missed mono-morph; try cache-only lookup. */

    MVMROOT2(tc, obj, name, {
        meth = MVM_6model_find_method_cache_only(tc, obj, name);
    });

    if (!MVM_is_null(tc, meth)) {
        /* Got it; cache. Must be careful due to threads
         * reading, races, etc. */
        uv_mutex_lock(&tc->instance->mutex_spesh_install);
        if (!tc->cur_frame->effective_spesh_slots[ss_idx + 1]) {
            MVM_ASSIGN_REF(tc, &(tc->cur_frame->spesh_cand->common.header),
                           tc->cur_frame->effective_spesh_slots[ss_idx + 1],
                           (MVMCollectable *)meth);
            MVM_barrier();
            MVM_ASSIGN_REF(tc, &(tc->cur_frame->spesh_cand->common.header),
                           tc->cur_frame->effective_spesh_slots[ss_idx],
                           (MVMCollectable *)STABLE(obj));
        }
        uv_mutex_unlock(&tc->instance->mutex_spesh_install);
        res->o = meth;
        return 0;
    }
    else {
        /* Fully late-bound. */
        MVM_6model_find_method(tc, obj, name, res, 1);
        return 1;
    }
}


/* Locates a method by name. Returns 1 if it exists; otherwise 0. */
static void late_bound_can_return(MVMThreadContext *tc, void *sr_data) {
    /* Transform to an integer result. */
    MVMRegister *reg = (MVMRegister *)sr_data;
    reg->i64 = !MVM_is_null(tc, reg->o) && IS_CONCRETE(reg->o) ? 1 : 0;
}

MVMint64 MVM_6model_can_method_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache;

    if (MVM_is_null(tc, obj)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Cannot look for method '%s' on a null object",
             c_name);
    }

    /* Consider the method cache. */

    MVMROOT2(tc, obj, name, {
        cache = get_method_cache(tc, STABLE(obj));
    });

    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = MVM_repr_at_key_o(tc, cache, name);
        if (!MVM_is_null(tc, meth)) {
            return 1;
        }
        if (STABLE(obj)->mode_flags & MVM_METHOD_CACHE_AUTHORITATIVE) {
            return 0;
        }
    }
    return -1;
}

void MVM_6model_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name, MVMRegister *res) {
    MVMObject *HOW = NULL, *find_method = NULL, *code = NULL;
    MVMCallsite *findmeth_callsite = NULL;

    MVMint64 can_cached;

    MVMROOT2(tc, obj, name, {
        can_cached = MVM_6model_can_method_cache_only(tc, obj, name);
    });

    if (can_cached == 0 || can_cached == 1) {
        res->i64 = can_cached;
        return;
    }

    /* If no method in cache and the cache is not authoritative, need to make
     * a late-bound call to find_method. */
    MVMROOT3(tc, obj, name, HOW, {
        HOW = MVM_6model_get_how(tc, STABLE(obj));
        find_method = MVM_6model_find_method_cache_only(tc, HOW,
            tc->instance->str_consts.find_method);
    });

    if (MVM_is_null(tc, find_method)) {
        /* This'll count as a "no"... */
        res->i64 = 0;
        return;
    }

    /* Set up the call, using the result register as the target. A little bad
     * as we're really talking about     */
    code = MVM_frame_find_invokee(tc, find_method, NULL);
    findmeth_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ_STR);
    MVM_args_setup_thunk(tc, res, MVM_RETURN_OBJ, findmeth_callsite);
    MVM_frame_special_return(tc, tc->cur_frame, late_bound_can_return, NULL, res, NULL);
    tc->cur_frame->args[0].o = HOW;
    tc->cur_frame->args[1].o = obj;
    tc->cur_frame->args[2].s = name;
    STABLE(code)->invoke(tc, code, findmeth_callsite, tc->cur_frame->args);
}

/* Checks if an object has a given type, delegating to the type_check or
 * accepts_type methods as needed. */
static void do_accepts_type_check(MVMThreadContext *tc, MVMObject *obj, MVMObject *type, MVMRegister *res) {
    MVMObject *HOW = NULL, *meth = NULL;

    MVMROOT3(tc, obj, type, HOW, {
        HOW = MVM_6model_get_how(tc, STABLE(type));
        meth = MVM_6model_find_method_cache_only(tc, HOW,
            tc->instance->str_consts.accepts_type);
    });

    if (!MVM_is_null(tc, meth)) {
        /* Set up the call, using the result register as the target. */
        MVMObject *code = MVM_frame_find_invokee(tc, meth, NULL);
        MVMCallsite *typecheck_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ_OBJ);

        MVM_args_setup_thunk(tc, res, MVM_RETURN_INT, typecheck_callsite);
        tc->cur_frame->args[0].o = HOW;
        tc->cur_frame->args[1].o = type;
        tc->cur_frame->args[2].o = obj;
        STABLE(code)->invoke(tc, code, typecheck_callsite, tc->cur_frame->args);
        return;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "Expected 'accepts_type' method, but none found in meta-object");
    }
}
typedef struct {
    MVMObject   *obj;
    MVMObject   *type;
    MVMRegister *res;
} AcceptsTypeSRData;

static void accepts_type_sr(MVMThreadContext *tc, void *sr_data) {
    AcceptsTypeSRData *atd = (AcceptsTypeSRData *)sr_data;
    MVMObject   *obj  = atd->obj;
    MVMObject   *type = atd->type;
    MVMRegister *res  = atd->res;
    MVM_free(atd);
    if (!res->i64)
        do_accepts_type_check(tc, obj, type, res);
}

static void mark_sr_data(MVMThreadContext *tc, MVMFrame *frame, MVMGCWorklist *worklist) {
    AcceptsTypeSRData *atd = (AcceptsTypeSRData *)frame->extra->special_return_data;
    MVM_gc_worklist_add(tc, worklist, &atd->obj);
    MVM_gc_worklist_add(tc, worklist, &atd->type);
}

static void free_sr_data(MVMThreadContext *tc, void *sr_data) {
    MVM_free(sr_data);
}

void MVM_6model_istype(MVMThreadContext *tc, MVMObject *obj, MVMObject *type, MVMRegister *res) {
    MVMObject **cache;
    MVMSTable  *st;
    MVMint64    mode;

    /* Null never type-checks. */
    if (MVM_is_null(tc, obj)) {
        res->i64 = 0;
        return;
    }

    st    = STABLE(obj);
    mode  = STABLE(type)->mode_flags & MVM_TYPE_CHECK_CACHE_FLAG_MASK;
    cache = st->type_check_cache;
    if (cache) {
        /* We have the cache, so just look for the type object we
         * want to be in there. */
        MVMint64 elems = STABLE(obj)->type_check_cache_length;
        MVMint64 i;
        for (i = 0; i < elems; i++) {
            if (cache[i] == type) {
                res->i64 = 1;
                return;
            }
        }

        /* If the type check cache is definitive, we're done. */
        if ((mode & MVM_TYPE_CHECK_CACHE_THEN_METHOD) == 0 &&
            (mode & MVM_TYPE_CHECK_NEEDS_ACCEPTS) == 0) {
            res->i64 = 0;
            return;
        }
    }

    /* If we get here, need to call .^type_check on the value we're
     * checking, unless it's an accepts check. */
    if (!cache || (mode & MVM_TYPE_CHECK_CACHE_THEN_METHOD)) {
        MVMObject *HOW = NULL, *meth = NULL;

        MVMROOT3(tc, obj, type, HOW, {
            HOW = MVM_6model_get_how(tc, st);
            meth = MVM_6model_find_method_cache_only(tc, HOW,
                tc->instance->str_consts.type_check);
        });
        if (!MVM_is_null(tc, meth)) {
            /* Set up the call, using the result register as the target. */
            MVMObject *code = MVM_frame_find_invokee(tc, meth, NULL);
            MVMCallsite *typecheck_callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_OBJ_OBJ_OBJ);

            MVM_args_setup_thunk(tc, res, MVM_RETURN_INT, typecheck_callsite);
            tc->cur_frame->args[0].o = HOW;
            tc->cur_frame->args[1].o = obj;
            tc->cur_frame->args[2].o = type;
            if (mode & MVM_TYPE_CHECK_NEEDS_ACCEPTS) {
                AcceptsTypeSRData *atd = MVM_malloc(sizeof(AcceptsTypeSRData));
                atd->obj = obj;
                atd->type = type;
                atd->res = res;
                MVM_frame_special_return(tc, tc->cur_frame, accepts_type_sr, free_sr_data,
                    atd, mark_sr_data);
            }
            STABLE(code)->invoke(tc, code, typecheck_callsite, tc->cur_frame->args);
            return;
        }
    }

    /* If the flag to call .accepts_type on the target value is set, do so. */
    if (mode & MVM_TYPE_CHECK_NEEDS_ACCEPTS) {
        do_accepts_type_check(tc, obj, type, res);
    }
    else {
        /* If all else fails... */
        res->i64 = 0;
    }
}

/* Checks if an object has a given type, using the cache only. */
MVMint64 MVM_6model_istype_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMObject *type) {
    if (!MVM_is_null(tc, obj)) {
        MVMuint16 i, elems = STABLE(obj)->type_check_cache_length;
        MVMObject  **cache = STABLE(obj)->type_check_cache;
        if (cache)
            for (i = 0; i < elems; i++) {
                if (cache[i] == type)
                    return 1;
            }
    }

    return 0;
}

/* Tries to do a type check using the cache. If the type is in the cache, then
 * result will be set to a true value and a true value will be returned. If it
 * is not in the cache and the cache is authoritative, then we know the answer
 * too; result is set to zero and a true value is returned. Otherwise, we can
 * not tell and a false value is returned and result is undefined. */
MVMint64 MVM_6model_try_cache_type_check(MVMThreadContext *tc, MVMObject *obj, MVMObject *type, MVMint32 *result) {
    if (!MVM_is_null(tc, obj)) {
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
    }
    return 0;
}

/* Default invoke function on STables; for non-invokable objects */
void MVM_6model_invoke_default(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "Cannot invoke this object (REPR: %s; %s)", REPR(invokee)->name, MVM_6model_get_debug_name(tc, invokee));
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
    MVM_free(st->invocation_spec);
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
