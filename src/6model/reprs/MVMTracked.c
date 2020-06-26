#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMTracked_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMTracked_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMTracked);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot clone an MVMTracked");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMTrackedBody *body = (MVMTrackedBody *)data;
    if (body->kind == MVM_CALLSITE_ARG_STR || body->kind == MVM_CALLSITE_ARG_OBJ)
        MVM_gc_worklist_add(tc, worklist, &(body->value.o));
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMTracked_initialize(MVMThreadContext *tc) {
    return &MVMTracked_this_repr;
}

static const MVMREPROps MVMTracked_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
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
    NULL, /* spesh */
    "MVMTracked", /* name */
    MVM_REPR_ID_MVMTracked,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Create a tracked wrapper of the specified value and kind. */
MVMObject * MVM_tracked_create(MVMThreadContext *tc, MVMRegister value, MVMCallsiteFlags kind) {
    MVMObject *tracked;
    if (kind & (MVM_CALLSITE_ARG_OBJ | MVM_CALLSITE_ARG_STR)) {
        MVMROOT(tc, value.o, {
            tracked = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTTracked);
        });
    }
    else {
        tracked = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTTracked);
    }
    ((MVMTracked *)tracked)->body.value = value;
    ((MVMTracked *)tracked)->body.kind = kind;
    return tracked;
}
