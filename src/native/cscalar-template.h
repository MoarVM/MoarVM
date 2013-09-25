#include "moarvm.h"

#define EVAL(MACRO) DO_EVAL(MACRO, REPR_NAME)
#define DO_EVAL(MACRO, ...) MACRO(__VA_ARGS__)

#define INIT(RN) MVM ## RN ## _initialize
#define NAME_STR(RN) #RN
#define REPR_ID(RN) MVM_REPR_ID_ ## RN
#define CAN_BOX(RN) MVM## RN ## _CAN_BOX

static const MVMREPROps this_repr;
static const MVMContainerSpec container_spec;

const MVMREPROps * EVAL(INIT)(MVMThreadContext *tc) {
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

static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src,
        MVMObject *dest_root, void *dest) {
    MVMPtrBody *src_body  = src;
    MVMPtrBody *dest_body = dest;

    dest_body->cobj = src_body->cobj;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->blob, src_body->blob);
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = EVAL(CAN_BOX);

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMPtrBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* noop */
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
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
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    EVAL(NAME_STR),
    EVAL(REPR_ID),
    0, /* refs_frames */
};

static void gc_mark_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    /* nothing to mark */
}

static const MVMContainerSpec container_spec = {
    NULL, /* name */
    NULL, /* fetch */
    NULL, /* store */
    NULL, /* store_unchecked */
    gc_mark_data,
    NULL, /* gc_free_data */
    NULL, /* serialize */
    NULL, /* deserialize */
};
