#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMKnowHOWREPR);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMObject *methods, *attributes;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *BOOTHash  = tc->instance->boot_types->BOOTHash;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTArray);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTHash);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&root);

    methods = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&methods);
    MVM_ASSIGN_REF(tc, root, ((MVMKnowHOWREPR *)root)->body.methods, methods);
    REPR(methods)->initialize(tc, STABLE(methods), methods, OBJECT_BODY(methods));

    attributes = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&attributes);
    MVM_ASSIGN_REF(tc, root, ((MVMKnowHOWREPR *)root)->body.attributes, attributes);
    REPR(attributes)->initialize(tc, STABLE(attributes), attributes, OBJECT_BODY(attributes));

    MVM_gc_root_temp_pop_n(tc, 5);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMKnowHOWREPRBody *src_body  = (MVMKnowHOWREPRBody *)src;
    MVMKnowHOWREPRBody *dest_body = (MVMKnowHOWREPRBody *)dest;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->methods, src_body->methods);
    MVM_ASSIGN_REF(tc, dest_root, dest_body->attributes, src_body->attributes);
    MVM_ASSIGN_REF(tc, dest_root, dest_body->name, src_body->name);
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

/* Deserializes the data. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMKnowHOWREPRBody *body = (MVMKnowHOWREPRBody *)data;
    MVM_ASSIGN_REF(tc, root, body->name, reader->read_str(tc, reader));
    MVM_ASSIGN_REF(tc, root, body->attributes, reader->read_ref(tc, reader));
    MVM_ASSIGN_REF(tc, root, body->methods, reader->read_ref(tc, reader));
}

/* Initializes the representation. */
MVMREPROps * MVMKnowHOWREPR_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    &MVM_REPR_DEFAULT_ATTR_FUNCS,
    &MVM_REPR_DEFAULT_BOX_FUNCS,
    &MVM_REPR_DEFAULT_POS_FUNCS,
    &MVM_REPR_DEFAULT_ASS_FUNCS,
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
    "KnowHOWREPR", /* name */
    MVM_REPR_ID_KnowHOWREPR,
    0, /* refs_frames */
};
