#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMNativeCall);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMNativeCallBody *src_body  = (MVMNativeCallBody *)src;
    MVMNativeCallBody *dest_body = (MVMNativeCallBody *)dest;

    /* Need a fresh handle for resource management purposes. */
    if (src_body->lib_name) {
        dest_body->lib_name = MVM_malloc(strlen(src_body->lib_name) + 1);
        strcpy(dest_body->lib_name, src_body->lib_name);
        dest_body->lib_handle = MVM_nativecall_load_lib(dest_body->lib_name);
    }

    /* Rest is just simple copying. */
    dest_body->entry_point = src_body->entry_point;
    dest_body->convention = src_body->convention;
    dest_body->num_args = src_body->num_args;
    if (src_body->arg_types) {
        dest_body->arg_types = MVM_malloc(sizeof(MVMint16) * (src_body->num_args ? src_body->num_args : 1));
        memcpy(dest_body->arg_types, src_body->arg_types, src_body->num_args * sizeof(MVMint16));
    }
    dest_body->ret_type = src_body->ret_type;
}



static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_INLINED,       /* inlineable */
    sizeof(MVMNativeCallBody) * 8,  /* bits */
    ALIGNOF(MVMNativeCallBody),     /* align */
    MVM_STORAGE_SPEC_BP_NONE,       /* boxed_primitive */
    0,                              /* can_box */
    0,                              /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* We can't actually serialize the handle, but since this REPR gets inlined
 * we just do nothing here since it may well have never been opened. Various
 * more involved approaches are possible. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
}
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNativeCall);
}
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    if (body->arg_info) {
        MVMint16 i;
        for (i = 0; i < body->num_args; i++)
            if (body->arg_info[i])
                MVM_gc_worklist_add(tc, worklist, &body->arg_info[i]);
    }
}

static void gc_cleanup(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    if (body->lib_name)
        MVM_free(body->lib_name);
    if (body->lib_handle)
        MVM_nativecall_free_lib(body->lib_handle);
    if (body->arg_types)
        MVM_free(body->arg_types);
    if (body->arg_info)
        MVM_free(body->arg_info);
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    gc_cleanup(tc, STABLE(obj), OBJECT_BODY(obj));
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMNativeCall_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
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
    gc_free,
    gc_cleanup,
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "NativeCall", /* name */
    MVM_REPR_ID_MVMNativeCall,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
