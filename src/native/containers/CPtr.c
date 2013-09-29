#include "moarvm.h"

static const MVMContainerSpecEx spec;

static int isctype(MVMObject *obj) {
    switch (REPR(obj)->ID) {
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

static void set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    if (st->REPR->ID != MVM_REPR_ID_CScalar)
        MVM_exception_throw_adhoc(tc,
                "can only make C scalar objects into CPtr containers");

    st->container_spec = &spec.basic;
    st->container_data = NULL;
}

static void configure_container_spec(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *config) {
    if (!isctype(config))
        MVM_exception_throw_adhoc(tc, "%s is no C type", REPR(config)->name);

    MVM_ASSIGN_REF(tc, st, st->container_data, config);
}

const MVMContainerConfigurer MVM_CONTAINER_CONF_CPtr = {
    set_container_spec,
    configure_container_spec
};


static void * unbox(MVMObject *obj) {
    return ((MVMPtr *)obj)->body.cobj;
}

static void fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    void *ptr = unbox(cont);
    MVMObject *type = STABLE(cont)->REPR_data;
    MVMBlob *blob = ((MVMPtr *)cont)->body.blob;
    MVMPtr *box;

    if (!type)
        MVM_exception_throw_adhoc(tc, "cannot fetch from uncomposed pointer");

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot fetch from null pointer");

    MVMROOT(tc, blob, {
        box = (MVMPtr *)MVM_repr_alloc_init(tc, type);
        box->body.cobj = *(void **)ptr;
        MVM_ASSIGN_REF(tc, box, box->body.blob, blob);
        res->o = (MVMObject *)box;
    });
}

static void store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    void *ptr = unbox(cont);

    if (!isctype(obj))
        MVM_exception_throw_adhoc(tc, "%s is no C type", REPR(obj)->name);

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot store into null pointer");

    if (((MVMPtr *)cont)->body.blob != ((MVMPtr *)obj)->body.blob)
        MVM_exception_throw_adhoc(tc, "cannot mix blobs");

    *(void **)ptr = unbox(obj);
}

static void store_unchecked(MVMThreadContext *tc, MVMObject *cont,
        MVMObject *obj) {
    *(void **)unbox(cont) = unbox(obj);
}

static void gc_mark_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    /* nothing to mark */
}

static const MVMContainerSpecEx spec = {
    { /* basic */
        "CPtr",
        fetch,
        store,
        store_unchecked,
        gc_mark_data,
        NULL, /* gc_free_data */
        NULL, /* serialize */
        NULL, /* deserialize */
    },
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
