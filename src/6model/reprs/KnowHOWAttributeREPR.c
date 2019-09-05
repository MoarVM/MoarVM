#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps KnowHOWAttributeREPR_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &KnowHOWAttributeREPR_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMKnowHOWAttributeREPR);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMKnowHOWAttributeREPRBody *src_body  = (MVMKnowHOWAttributeREPRBody *)src;
    MVMKnowHOWAttributeREPRBody *dest_body = (MVMKnowHOWAttributeREPRBody *)dest;
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->name, src_body->name);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->type, src_body->type);
    dest_body->box_target = src_body->box_target;
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

/* Serializes the data. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMKnowHOWAttributeREPRBody *body = (MVMKnowHOWAttributeREPRBody *)data;
    MVM_serialization_write_str(tc, writer, body->name);
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMKnowHOWAttributeREPRBody *body = (MVMKnowHOWAttributeREPRBody *)data;
    MVM_ASSIGN_REF(tc, &(root->header), body->name, MVM_serialization_read_str(tc, reader));
    MVM_ASSIGN_REF(tc, &(root->header), body->type, tc->instance->KnowHOW);
}

/* Initializes the representation. */
const MVMREPROps * MVMKnowHOWAttributeREPR_initialize(MVMThreadContext *tc) {
    return &KnowHOWAttributeREPR_this_repr;
}

static const MVMREPROps KnowHOWAttributeREPR_this_repr = {
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
    serialize,
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
    NULL, /* spesh */
    NULL, /* jit */
    "KnowHOWAttributeREPR", /* name */
    MVM_REPR_ID_KnowHOWAttributeREPR,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
