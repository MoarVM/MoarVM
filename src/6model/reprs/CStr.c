#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps CStr_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &CStr_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCStr);
    });

    return st->WHAT;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* TODO: move encoding stuff into here */
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCStrBody *src_body  = (MVMCStrBody *)src;
    MVMCStrBody *dest_body = (MVMCStrBody *)dest;
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->orig, src_body->orig);
    dest_body->cstr = src_body->cstr;
}

static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVMCStrBody *body = (MVMCStrBody *)data;
    MVM_ASSIGN_REF(tc, &(root->header), body->orig, value);
#ifdef MVM_USE_MIMALLOC
    body->cstr = MVM_string_utf8_encode_C_string_malloc(tc, value);
#else
    body->cstr = MVM_string_utf8_encode_C_string(tc, value);
#endif
}

static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCStrBody *body = (MVMCStrBody *)data;
    return body->orig;
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE,       /* inlineable */
    sizeof(void *) * 8,               /* bits */
    ALIGNOF(void *),                  /* align */
    MVM_STORAGE_SPEC_BP_STR,          /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_STR,     /* can_box */
    0,                                /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCStrBody *body = (MVMCStrBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->orig);
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCStr *cstr = (MVMCStr *)obj;
    if (obj && cstr->body.cstr)
#ifdef MVM_USE_MIMALLOC
        free(cstr->body.cstr);
#else
        MVM_free(cstr->body.cstr);
#endif
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCStr);
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMString *orig = MVM_serialization_read_str(tc, reader);
    MVMCStrBody *body = (MVMCStrBody *)data;
    MVM_ASSIGN_REF(tc, &(root->header), body->orig, orig);

#ifdef MVM_USE_MIMALLOC
    body->cstr = MVM_string_utf8_encode_C_string_malloc(tc, orig);
#else
    body->cstr = MVM_string_utf8_encode_C_string(tc, orig);
#endif
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVM_serialization_write_str(tc, writer, ((MVMCStrBody *)data)->orig);
}

/* Initializes the representation. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc) {
    return &CStr_this_repr;
}

static const MVMREPROps CStr_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        MVM_REPR_DEFAULT_SET_INT,
        MVM_REPR_DEFAULT_GET_INT,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        set_str,
        get_str,
        MVM_REPR_DEFAULT_SET_UINT,
        MVM_REPR_DEFAULT_GET_UINT,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
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
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "CStr", /* name */
    MVM_REPR_ID_MVMCStr,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
