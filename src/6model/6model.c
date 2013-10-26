#include "moar.h"

/* Dummy callsite for find_method. */
static MVMCallsiteEntry fm_flags[] = { MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_OBJ,
                                       MVM_CALLSITE_ARG_STR };
static MVMCallsite     fm_callsite = { fm_flags, 3, 3, 0 };

/* Locates a method by name, checking in the method cache only. */
MVMObject * MVM_6model_find_method_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache = STABLE(obj)->method_cache;
    if (cache && IS_CONCRETE(cache))
        return MVM_repr_at_key_boxed(tc, cache, name);
    return NULL;
}

/* Locates a method by name. Returns the method if it exists, or throws an
 * exception if it can not be found. */
void MVM_6model_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name, MVMRegister *res) {
    MVMObject *HOW, *find_method, *code;

    /* First try to find it in the cache. If we find it, we have a result.
     * If we don't find it, but the cache is authoritative, this is also a
     * good enough result. */
    MVMObject *cache = STABLE(obj)->method_cache;
    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = MVM_repr_at_key_boxed(tc, cache, name);
        if (meth || STABLE(obj)->mode_flags & MVM_METHOD_CACHE_AUTHORITATIVE) {
            res->o = meth;
            return;
        }
    }
    
    /* Otherwise, need to call the find_method method. We make the assumption
     * that the invocant's meta-object's type is composed. */
    HOW = STABLE(obj)->HOW;
    find_method = MVM_6model_find_method_cache_only(tc, HOW,
        tc->instance->str_consts.find_method);
    if (find_method == NULL)
        MVM_exception_throw_adhoc(tc,
            "Cannot find method %s: no method cache and no .^find_method",
             MVM_string_utf8_encode_C_string(tc, name));

    /* Set up the call, using the result register as the target. */
    code = MVM_frame_find_invokee(tc, find_method);
    tc->cur_frame->return_value   = res;
    tc->cur_frame->return_type    = MVM_RETURN_OBJ;
    tc->cur_frame->return_address = *(tc->interp_cur_op);
    tc->cur_frame->args[0].o = HOW;
    tc->cur_frame->args[1].o = obj;
    tc->cur_frame->args[2].s = name;
    STABLE(code)->invoke(tc, code, &fm_callsite, tc->cur_frame->args);
}

/* Locates a method by name. Returns 1 if it exists; otherwise 0. */
MVMint64 MVM_6model_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache = STABLE(obj)->method_cache;
    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = MVM_repr_at_key_boxed(tc, cache, name);
        return meth ? 1 : 0;
    }
    return 0;
}

/* Checks if an object has a given type, using the cache only. */
MVMint64 MVM_6model_istype_cache_only(MVMThreadContext *tc, MVMObject *obj, MVMObject *type) {
    if (obj != NULL) {
        MVMint64 i, result = 0, elems = STABLE(obj)->type_check_cache_length;
        MVMObject **cache = STABLE(obj)->type_check_cache;
        if (cache)
            for (i = 0; i < elems; i++) {
                if (cache[i] == type) {
                    result = 1;
                    break;
                }
            }
        return result;
    }
    else {
        return 0;
    }
}

/* Default invoke function on STables; for non-invokable objects */
void MVM_6model_invoke_default(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "non-invokable object is non-invokable");
}

void MVM_6model_stable_gc_free(MVMThreadContext *tc, MVMSTable *st) {
    /* First have it free its repr_data if it wants. */
    if (st->REPR->gc_free_repr_data)
        st->REPR->gc_free_repr_data(tc, st);

    /* free various storage. */
    MVM_checked_free_null(st->vtable);
    MVM_checked_free_null(st->type_check_cache);
    if (st->container_spec && st->container_spec->gc_free_data)
        st->container_spec->gc_free_data(tc, st);
    MVM_checked_free_null(st->invocation_spec);
    MVM_checked_free_null(st->boolification_spec);
}
