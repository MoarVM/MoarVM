#include "moarvm.h"

static const MVMREPROps this_repr;
static const MVMContainerSpec container_spec;

static int isctype(MVMObject *obj) {
    switch (REPR(obj)->ID) {
        case MVM_REPR_ID_CScalar:
        case MVM_REPR_ID_CPtr:
        case MVM_REPR_ID_CArray:
        case MVM_REPR_ID_CStruct:
        case MVM_REPR_ID_CUnion:
        case MVM_REPR_ID_CFlexStruct:
            return 1;

        default:
            return 0;
    }
}

const MVMREPROps * MVMCPtr_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMPtr);
        st->container_spec = &container_spec;
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVMPtrBody *body = data;

    body->cobj = NULL;
    body->blob = NULL;
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMPtrBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    if (st->REPR_data)
        MVM_gc_worklist_add(tc, worklist, &st->REPR_data);
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    if (!isctype(info))
        MVM_exception_throw_adhoc(tc, "%s is no C type", REPR(info)->name);

    MVM_ASSIGN_REF(tc, st, st->REPR_data, info);
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    NULL, /* copy_to */
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    NULL, /* gc_free_repr_data */
    compose,
    "CPtr",
    MVM_REPR_ID_CPtr,
    0, /* refs_frames */
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

static const MVMContainerSpec container_spec = {
    NULL, /* name */
    fetch,
    store,
    store_unchecked,
    gc_mark_data,
    NULL, /* gc_free_data */
    NULL, /* serialize */
    NULL, /* deserialize */
};
