#include "moarvm.h"

#define ALIGNOF(type) \
    ((MVMuint16)offsetof(struct { char dummy; type member; }, member))

MVMObject * MVM_native_bloballoc(MVMThreadContext *tc, MVMuint64 size) {
    MVMPtr *ptr = (MVMPtr *)MVM_gc_allocate_object(tc,
            STABLE(tc->instance->raw_types.RawPtr));

    MVMROOT(tc, ptr, {
        MVMBlob *blob = (MVMBlob *)MVM_gc_allocate_object(tc,
                STABLE(tc->instance->raw_types.RawBlob));
        void *storage = malloc(size);

        blob->body.storage = storage;
        blob->body.size    = size;
        blob->body.refmap  = NULL;

        ptr->body.cobj = storage;
        MVM_ASSIGN_REF(tc, ptr, ptr->body.blob, blob);
    });

    return (MVMObject *)ptr;
}

MVMObject * MVM_native_ptrcast(MVMThreadContext *tc, MVMObject *src,
        MVMObject *type, MVMint64 offset) {
    MVM_exception_throw_adhoc(tc, "TODO");
}

MVMuint64 MVM_native_csizeof(MVMThreadContext *tc, MVMObject *obj) {
    switch(REPR(obj)->ID) {
        default:
            MVM_exception_throw_adhoc(tc, "not a C type");
    }
}

MVMuint64 MVM_native_calignof(MVMThreadContext *tc, MVMObject *obj) {
    switch(REPR(obj)->ID) {
        default:
            MVM_exception_throw_adhoc(tc, "not a C type");
    }
}

MVMuint64 MVM_native_coffsetof(MVMThreadContext *tc, MVMObject *obj,
        MVMString *member) {
    if (REPR(obj)->ID != MVM_REPR_ID_CStruct)
        MVM_exception_throw_adhoc(tc, "not a C struct");

    MVM_exception_throw_adhoc(tc, "TODO");
}
