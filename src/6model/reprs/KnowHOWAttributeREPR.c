#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMKnowHOWAttributeREPR);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMKnowHOWAttributeREPRBody *src_body  = (MVMKnowHOWAttributeREPRBody *)src;
    MVMKnowHOWAttributeREPRBody *dest_body = (MVMKnowHOWAttributeREPRBody *)dest;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->name, src_body->name);
    MVM_ASSIGN_REF(tc, dest_root, dest_body->type, src_body->type);
    dest_body->box_target = src_body->box_target;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMKnowHOWAttributeREPRBody *body = (MVMKnowHOWAttributeREPRBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->name);
    MVM_gc_worklist_add(tc, worklist, &body->type);
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMKnowHOWAttributeREPR);
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMKnowHOWAttributeREPRBody *body = (MVMKnowHOWAttributeREPRBody *)data;
    MVM_ASSIGN_REF(tc, root, body->name, reader->read_str(tc, reader));
    MVM_ASSIGN_REF(tc, root, body->type, tc->instance->KnowHOW);
}

/* Initializes the representation. */
MVMREPROps * MVMKnowHOWAttributeREPR_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    NULL, /* attr_funcs */
    NULL, /* box_funcs  */
    NULL, /* pos_funcs */
    NULL, /* ass_funcs */
    NULL, /* elems */
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    deserialize,
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* name */
    0, /* ID */
    0, /* refs_frames */
};