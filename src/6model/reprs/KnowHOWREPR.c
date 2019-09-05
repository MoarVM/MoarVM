#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps KnowHOWREPR_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &KnowHOWREPR_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMKnowHOWREPR);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMObject *methods, *attributes, *BOOTArray;
    MVMObject * const BOOTHash  = tc->instance->boot_types.BOOTHash;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&root);

    methods = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&methods);
    MVM_ASSIGN_REF(tc, &(root->header), ((MVMKnowHOWREPR *)root)->body.methods, methods);

    BOOTArray  = tc->instance->boot_types.BOOTArray;
    attributes = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_ASSIGN_REF(tc, &(root->header), ((MVMKnowHOWREPR *)root)->body.attributes, attributes);

    MVM_gc_root_temp_pop_n(tc, 2);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMKnowHOWREPRBody *src_body  = (MVMKnowHOWREPRBody *)src;
    MVMKnowHOWREPRBody *dest_body = (MVMKnowHOWREPRBody *)dest;
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->methods, src_body->methods);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->attributes, src_body->attributes);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->name, src_body->name);
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
    MVMKnowHOWREPRBody *body = (MVMKnowHOWREPRBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->methods);
    MVM_gc_worklist_add(tc, worklist, &body->attributes);
    MVM_gc_worklist_add(tc, worklist, &body->name);
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMKnowHOWREPR);
}

/* Serializes the data. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMKnowHOWREPRBody *body = (MVMKnowHOWREPRBody *)data;
    MVM_serialization_write_str(tc, writer, body->name);
    MVM_serialization_write_ref(tc, writer, body->attributes);
    MVM_serialization_write_ref(tc, writer, body->methods);
}

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMKnowHOWREPRBody *body = (MVMKnowHOWREPRBody *)data;
    MVM_ASSIGN_REF(tc, &(root->header), body->name, MVM_serialization_read_str(tc, reader));
    MVM_ASSIGN_REF(tc, &(root->header), body->attributes, MVM_serialization_read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, &(root->header), body->methods, MVM_serialization_read_ref(tc, reader));
}

/* Initializes the representation. */
const MVMREPROps * MVMKnowHOWREPR_initialize(MVMThreadContext *tc) {
    return &KnowHOWREPR_this_repr;
}

static const MVMREPROps KnowHOWREPR_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
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
    "KnowHOWREPR", /* name */
    MVM_REPR_ID_KnowHOWREPR,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
