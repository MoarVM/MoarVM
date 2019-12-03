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
#ifndef HAVE_LIBFFI
    MVMNativeCallBody *body = (MVMNativeCallBody *)data;
    MVMint16 i = 0;
    MVM_serialization_write_cstr(
        tc,
        writer,
        !MVM_is_null(tc, body->resolve_lib_name) && !MVM_is_null(tc, body->resolve_lib_name_arg)
            ? NULL
            : body->lib_name
    );
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
    MVM_serialization_write_ref(tc, writer, (MVMObject*)body->resolve_lib_name);
    MVM_serialization_write_ref(tc, writer, (MVMObject*)body->resolve_lib_name_arg);
#endif
}
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNativeCall);
}

typedef struct ResolverData {
    MVMObject *site;
    MVMRegister args[1];
} ResolverData;
static void callback_invoke(MVMThreadContext *tc, void *data) {
    /* Invoke the coderef, to set up the nested interpreter. */
    ResolverData *r = (ResolverData*)data;
    MVMNativeCallBody *body = MVM_nativecall_get_nc_body(tc, r->site);
    MVMObject *code = body->resolve_lib_name;
    MVMCallsite *callsite = MVM_callsite_get_common(tc, MVM_CALLSITE_ID_INV_ARG);
    r->args[0].o = body->resolve_lib_name_arg;
    STABLE(code)->invoke(tc, code, callsite, r->args);

    /* Ensure we exit interp after callback. */
    tc->thread_entry_frame = tc->cur_frame;
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
        body->resolve_lib_name = MVM_serialization_read_ref(tc, reader);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&body->resolve_lib_name);
        body->resolve_lib_name_arg = MVM_serialization_read_ref(tc, reader);
        MVM_gc_root_temp_push(tc, (MVMCollectable **)&body->resolve_lib_name_arg);

        if (!MVM_is_null(tc, body->resolve_lib_name) && !MVM_is_null(tc, body->resolve_lib_name_arg)) {
            MVMRegister res = {NULL};
            ResolverData data = {root, {NULL}};
            MVM_gc_allocate_gen2_default_clear(tc);
            /* Call into a nested interpreter (since we already are in one). Need to
             * save a bunch of state around each side of this. */
            {
                MVMuint8 **backup_interp_cur_op         = tc->interp_cur_op;
                MVMuint8 **backup_interp_bytecode_start = tc->interp_bytecode_start;
                MVMRegister **backup_interp_reg_base    = tc->interp_reg_base;
                MVMCompUnit **backup_interp_cu          = tc->interp_cu;

                MVMFrame *backup_cur_frame              = MVM_frame_force_to_heap(tc, tc->cur_frame);
                MVMFrame *backup_thread_entry_frame     = tc->thread_entry_frame;
                void **backup_jit_return_address        = tc->jit_return_address;
                tc->jit_return_address                  = NULL;
                MVMROOT5(tc, backup_cur_frame, backup_thread_entry_frame, res.o, root, data.site, {
                    MVMuint32 backup_mark                   = MVM_gc_root_temp_mark(tc);
                    jmp_buf backup_interp_jump;
                    memcpy(backup_interp_jump, tc->interp_jump, sizeof(jmp_buf));


                    tc->cur_frame->return_value = &res;
                    tc->cur_frame->return_type  = MVM_RETURN_OBJ;

                    MVM_interp_run(tc, callback_invoke, &data);

                    tc->interp_cur_op         = backup_interp_cur_op;
                    tc->interp_bytecode_start = backup_interp_bytecode_start;
                    tc->interp_reg_base       = backup_interp_reg_base;
                    tc->interp_cu             = backup_interp_cu;
                    tc->cur_frame             = backup_cur_frame;
                    tc->current_frame_nr      = backup_cur_frame->sequence_nr;
                    tc->thread_entry_frame    = backup_thread_entry_frame;
                    tc->jit_return_address    = backup_jit_return_address;

                    memcpy(tc->interp_jump, backup_interp_jump, sizeof(jmp_buf));
                    MVM_gc_root_temp_mark_reset(tc, backup_mark);
                });
                body = MVM_nativecall_get_nc_body(tc, root); /* refresh body after potential GC move */
            }
            MVM_gc_allocate_gen2_default_set(tc);

            /* Handle return value. */
            if (res.o) {
                MVMContainerSpec const *contspec = STABLE(res.o)->container_spec;
                if (contspec && contspec->fetch_never_invokes)
                    contspec->fetch(tc, res.o, &res);
            }
            MVM_gc_root_temp_pop_n(tc, 2);
            body->lib_name = MVM_string_utf8_encode_C_string(tc, MVM_repr_get_str(tc, res.o));
        }
        if (body->lib_name && body->sym_name && !body->lib_handle) {
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
    if (body->resolve_lib_name)
        MVM_gc_worklist_add(tc, worklist, &body->resolve_lib_name);
    if (body->resolve_lib_name_arg)
        MVM_gc_worklist_add(tc, worklist, &body->resolve_lib_name_arg);
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
