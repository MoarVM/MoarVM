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
    if (src_body->jitcode) {
        dest_body->jitcode = MVM_jit_code_copy(tc, src_body->jitcode);
    }
    else
        dest_body->jitcode = NULL;
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
#ifndef HAVE_LIBFFI
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    MVMint16 i = 0;
    MVM_serialization_write_cstr(tc, writer, body->serialize_lib_name ? body->lib_name : NULL);
    MVM_serialization_write_cstr(tc, writer, body->sym_name);
    MVM_serialization_write_int(tc, writer, body->convention);
    MVM_serialization_write_int(tc, writer, body->num_args);
    MVM_serialization_write_int(tc, writer, body->ret_type);
    /* TODO ffi support */
    for (i = 0; i < body->num_args; i++) {
        MVM_serialization_write_int(tc, writer, body->arg_types[i]);
    }
    for (i = 0; i < body->num_args; i++) {
        MVM_serialization_write_ref(tc, writer, body->arg_info[i]);
    }
#endif
}
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNativeCall);
}
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
#ifndef HAVE_LIBFFI
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    MVMint16 i = 0;
    if (reader->root.version >= 22) {
        body->lib_name = MVM_serialization_read_cstr(tc, reader);
        body->sym_name = MVM_serialization_read_cstr(tc, reader);
        body->convention = MVM_serialization_read_int(tc, reader);
        body->num_args = MVM_serialization_read_int(tc, reader);
        body->ret_type = MVM_serialization_read_int(tc, reader);
        body->arg_types = MVM_malloc(body->num_args * sizeof(MVMint16));
        body->arg_info  = MVM_malloc(body->num_args * sizeof(MVMObject*));
        /* TODO ffi support */
        for (i = 0; i < body->num_args; i++) {
            body->arg_types[i] = MVM_serialization_read_int(tc, reader);
        }
        for (i = 0; i < body->num_args; i++) {
            body->arg_info[i] = MVM_serialization_read_ref(tc, reader);
        }
        if (body->lib_name && body->sym_name) {
            MVM_nativecall_setup(tc, body, 0);
        }
    }
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
    if (body->jitcode)
        MVM_jit_code_destroy(tc, body->jitcode);
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
    return (body->lib_handle ? 1 + (body->jitcode ? 1 : 0) : 0);
}

void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMRegister value, MVMuint16 kind) {
    char *c_name = MVM_string_utf8_encode_C_string(tc, name);
    if (strcmp(c_name, "serialize_lib_name") != 0) {
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(
            tc,
            waste,
            "P6opaque: no such attribute '%s' on type %s when trying to bind a value",
            c_name,
            MVM_6model_get_debug_name(tc, class_handle)
        );
    }
    MVM_free(c_name);

    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    body->serialize_lib_name = value.u8;

}

void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    body->serialize_lib_name = 1;
}

static const MVMREPROps NativeCall_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
    copy_to,
    {
        MVM_REPR_DEFAULT_GET_ATTRIBUTE,
        bind_attribute,
        MVM_REPR_DEFAULT_HINT_FOR,
        MVM_REPR_DEFAULT_IS_ATTRIBUTE_INITIALIZED,
        MVM_REPR_DEFAULT_ATTRIBUTE_AS_ATOMIC
    },
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
