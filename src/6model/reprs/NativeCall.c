#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps NativeCall_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &NativeCall_this_repr, HOW);

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
        if (src_body->entry_point)
            dest_body->lib_handle = MVM_nativecall_load_lib(dest_body->lib_name);
    }

    /* Rest is just simple copying. */
    if (src_body->sym_name) {
        dest_body->sym_name = MVM_malloc(strlen(src_body->sym_name) + 1);
        strcpy(dest_body->sym_name, src_body->sym_name);
    }
    dest_body->entry_point = src_body->entry_point;
    dest_body->convention = src_body->convention;
    dest_body->num_args = src_body->num_args;
    if (src_body->arg_types) {
        dest_body->arg_types = MVM_malloc(sizeof(MVMint16) * (src_body->num_args ? src_body->num_args : 1));
        memcpy(dest_body->arg_types, src_body->arg_types, src_body->num_args * sizeof(MVMint16));
    }
    if (src_body->arg_info) {
        dest_body->arg_info = MVM_malloc(sizeof(MVMObject*) * (src_body->num_args ? src_body->num_args : 1));
        memcpy(dest_body->arg_info, src_body->arg_info, src_body->num_args * sizeof(MVMObject*));
    }
    dest_body->ret_type = src_body->ret_type;
#ifdef HAVE_LIBFFI
    dest_body->ffi_ret_type = src_body->ffi_ret_type;
    if (src_body->ffi_arg_types) {
        dest_body->ffi_arg_types = MVM_malloc(sizeof(ffi_type *) * (dest_body->num_args ? dest_body->num_args : 1));
        memcpy(dest_body->ffi_arg_types, src_body->ffi_arg_types, sizeof(ffi_type *) * (dest_body->num_args ? dest_body->num_args : 1));
    }
#endif
    dest_body->resolve_lib_name = src_body->resolve_lib_name;
    dest_body->resolve_lib_name_arg = src_body->resolve_lib_name_arg;
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
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    MVMint16 i = 0;
    MVM_serialization_write_cstr(
        tc,
        writer,
        !body->resolve_lib_name && !MVM_is_null(tc, body->resolve_lib_name_arg)
            ? NULL
            : body->lib_name
    );
    MVM_serialization_write_cstr(tc, writer, body->sym_name);
    MVM_serialization_write_int(tc, writer, body->convention);
    MVM_serialization_write_int(tc, writer, body->num_args);
    MVM_serialization_write_int(tc, writer, body->ret_type);
    for (i = 0; i < body->num_args; i++) {
        MVM_serialization_write_int(tc, writer, body->arg_types[i]);
    }
    for (i = 0; i < body->num_args; i++) {
        MVM_serialization_write_ref(tc, writer, body->arg_info[i]);
    }
    MVM_serialization_write_ref(tc, writer, (MVMObject*)body->resolve_lib_name);
    MVM_serialization_write_ref(tc, writer, (MVMObject*)body->resolve_lib_name_arg);
}
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNativeCall);
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    MVMint16 i = 0;
    body->lib_name = MVM_serialization_read_cstr(tc, reader, NULL);
    body->sym_name = MVM_serialization_read_cstr(tc, reader, NULL);
    body->convention = MVM_serialization_read_int(tc, reader);
    body->num_args = MVM_serialization_read_int(tc, reader);
    body->ret_type = MVM_serialization_read_int(tc, reader);
    body->arg_types = body->num_args ? MVM_malloc(body->num_args * sizeof(MVMint16)) : NULL;
    body->arg_info  = body->num_args ? MVM_malloc(body->num_args * sizeof(MVMObject*)) : NULL;
    for (i = 0; i < body->num_args; i++) {
        body->arg_types[i] = MVM_serialization_read_int(tc, reader);
    }
    for (i = 0; i < body->num_args; i++) {
        body->arg_info[i] = MVM_serialization_read_ref(tc, reader);
    }
    body->resolve_lib_name = (MVMCode *)MVM_serialization_read_ref(tc, reader);
    body->resolve_lib_name_arg = MVM_serialization_read_ref(tc, reader);
#ifdef HAVE_LIBFFI
    body->ffi_arg_types = MVM_malloc(sizeof(ffi_type *) * (body->num_args ? body->num_args : 1));
    for (i = 0; i < body->num_args; i++) {
        body->ffi_arg_types[i] = MVM_nativecall_get_ffi_type(tc, body->arg_types[i]);
    }
    body->ffi_ret_type = MVM_nativecall_get_ffi_type(tc, body->ret_type);
#endif
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    if (body->arg_info) {
        MVMint16 i;
        for (i = 0; i < body->num_args; i++)
            if (body->arg_info[i])
                MVM_gc_worklist_add(tc, worklist, &body->arg_info[i]);
    }
    if (body->resolve_lib_name)
        MVM_gc_worklist_add(tc, worklist, &body->resolve_lib_name);
    if (body->resolve_lib_name_arg)
        MVM_gc_worklist_add(tc, worklist, &body->resolve_lib_name_arg);
}

static void gc_cleanup(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    if (body->lib_name)
        MVM_free(body->lib_name);
    if (body->sym_name)
        MVM_free(body->sym_name);
/* FIXME don't free the library unconditionally, as the handle will be shared among NativeCall sites
 * Also if we're called by repossession, we would use any initialized state of the library
    if (body->lib_handle)
        MVM_nativecall_free_lib(body->lib_handle);
*/
    if (body->arg_types)
        MVM_free(body->arg_types);
    if (body->arg_info)
        MVM_free(body->arg_info);
#ifdef HAVE_LIBFFI
    if (body->ffi_arg_types)
        MVM_free(body->ffi_arg_types);
#endif
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
    return &NativeCall_this_repr;
}

/* gets the setup state */
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    return (body->lib_handle ? 1 : 0);
}

static const MVMREPROps NativeCall_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        MVM_REPR_DEFAULT_SET_INT,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        MVM_REPR_DEFAULT_SET_UINT,
        MVM_REPR_DEFAULT_GET_UINT,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },
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
