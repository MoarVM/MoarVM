#include "moarvm.h"

typedef void func(void);

static int isptr(MVMObject *obj) {
    switch (REPR(obj)->ID) {
        case MVM_REPR_ID_VMPtr:
        case MVM_REPR_ID_CScalar:
        case MVM_REPR_ID_CArray:
        case MVM_REPR_ID_CStruct:
        case MVM_REPR_ID_CUnion:
        case MVM_REPR_ID_CFlexStruct:
            return 1;

        default:
            return 0;
    }
}

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
    void *cptr;
    MVMBlob *blob;
    MVMPtr *obj;

    if (!isptr(src))
        MVM_exception_throw_adhoc(tc, "cannot cast non-pointer");

    if (!isptr(type))
        MVM_exception_throw_adhoc(tc, "cannot cast to non-pointer");

    cptr = (char *)((MVMPtr *)src)->body.cobj + offset;
    blob = ((MVMPtr *)src)->body.blob;

    if (blob)
    {
        uintptr_t size  = (uintptr_t)MVM_native_csizeof(tc, type);
        uintptr_t lower = (uintptr_t)blob->body.storage;
        uintptr_t upper = lower + blob->body.size;
        uintptr_t value = (uintptr_t)cptr;

        if (value < lower || value + size > upper)
            MVM_exception_throw_adhoc(tc, "blob overflow");
    }

    MVMROOT(tc, blob, {
        obj = (MVMPtr *)MVM_repr_alloc_init(tc, type);
        obj->body.cobj = cptr;
        MVM_ASSIGN_REF(tc, obj, obj->body.blob, blob);
    });

    return (MVMObject *)obj;
}

MVMuint64 MVM_native_csizeof(MVMThreadContext *tc, MVMObject *obj) {
    switch (REPR(obj)->ID) {
        case MVM_REPR_ID_CScalar: {
            MVMuint64 *data = STABLE(obj)->container_data;

            if (!data)
                MVM_exception_throw_adhoc(tc,
                        "cannot get size of incomplete CScalar");

            return data[0];
        }

        case MVM_REPR_ID_CArray: {
            MVMCArraySpec *spec = STABLE(obj)->REPR_data;

            if (!spec)
                MVM_exception_throw_adhoc(tc,
                        "cannot get size of uncomposed C array");

            return spec->elem_count * spec->elem_size;
        }

        case MVM_REPR_ID_CStruct: {
            MVMCStructSpec *spec = STABLE(obj)->REPR_data;

            if (!spec)
                MVM_exception_throw_adhoc(tc,
                        "cannot get size of uncomposed C struct");

            return spec->size;
        }

        case MVM_REPR_ID_CUnion: {
            MVMCUnionSpec *spec = STABLE(obj)->REPR_data;

            if (!spec)
                MVM_exception_throw_adhoc(tc,
                        "cannot get size of uncomposed C union");

            return spec->size;
        }

        case MVM_REPR_ID_CFlexStruct:
            MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);

        default:
            MVM_exception_throw_adhoc(tc, "not a C type");
    }
}

MVMuint64 MVM_native_calignof(MVMThreadContext *tc, MVMObject *obj) {
    switch (REPR(obj)->ID) {
        case MVM_REPR_ID_CScalar: {
            MVMuint64 *data = STABLE(obj)->container_data;

            if (!data)
                MVM_exception_throw_adhoc(tc,
                        "cannot get alignment of incomplete CScalar");

            return data[1];
        }

        case MVM_REPR_ID_CArray: {
            if (!STABLE(obj)->REPR_data)
                MVM_exception_throw_adhoc(tc,
                        "cannot get alignment of uncomposed C array");

            return MVM_native_calignof(tc, STABLE(obj)->REPR_data);
        }

        case MVM_REPR_ID_CStruct: {
            MVMCStructSpec *spec = STABLE(obj)->REPR_data;

            if (!spec)
                MVM_exception_throw_adhoc(tc,
                        "cannot get alignment of uncomposed C struct");

            return spec->align;
        }

        case MVM_REPR_ID_CUnion: {
            MVMCUnionSpec *spec = STABLE(obj)->REPR_data;

            if (!spec)
                MVM_exception_throw_adhoc(tc,
                        "cannot get alignment of uncomposed C union");

            return spec->align;
        }

        case MVM_REPR_ID_CFlexStruct:
            MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);

        default:
            MVM_exception_throw_adhoc(tc, "not a C type");
    }
}

MVMuint64 MVM_native_coffsetof(MVMThreadContext *tc, MVMObject *obj,
        MVMString *member) {
    MVMCStructSpec *spec = STABLE(obj)->REPR_data;
    MVMint64 hint;

    if (REPR(obj)->ID != MVM_REPR_ID_CStruct)
        MVM_exception_throw_adhoc(tc, "not a C struct");

    if (!spec)
        MVM_exception_throw_adhoc(tc,
                "cannot get offsets from uncomposed C struct");

    hint = REPR(obj)->attr_funcs.hint_for(tc, STABLE(obj), NULL, member);

    if (hint < 0)
        MVM_exception_throw_adhoc(tc, "unknown attribute");

    return spec->members[hint].offset;
}
