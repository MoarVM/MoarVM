#include "moarvm.h"

/* Locates a method by name. Returns the method if it exists, or throws an
 * exception if it can not be found. */
MVMObject * MVM_6model_find_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache = STABLE(obj)->method_cache;
    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = REPR(cache)->ass_funcs->at_key_boxed(tc, STABLE(cache),
            cache, OBJECT_BODY(cache), (MVMObject *)name);
        if (meth)
            return meth;
        else
            MVM_exception_throw_adhoc(tc, "Method %s not found in cache, and late-bound dispatch NYI",
                MVM_string_utf8_encode_C_string(tc, name));
    }
    else {
        MVM_exception_throw_adhoc(tc, "Missing method cache; late-bound dispatch NYI");
    }
}

/* Locates a method by name. Returns 1 if it exists; otherwise 0. */
MVMint64 MVM_6model_can_method(MVMThreadContext *tc, MVMObject *obj, MVMString *name) {
    MVMObject *cache = STABLE(obj)->method_cache;
    if (cache && IS_CONCRETE(cache)) {
        MVMObject *meth = REPR(cache)->ass_funcs->at_key_boxed(tc, STABLE(cache),
            cache, OBJECT_BODY(cache), (MVMObject *)name);
        return meth ? 1 : 0;
    }
    return 0;
}

/* Default invoke function on STables; for non-invokable objects */
void MVM_6model_invoke_default(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "non-invokable object is non-invokable");
}
