#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMNativeRef);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with repr MVMNativeRef");
}

/* Set the size of objects on the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMNativeRef);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)st->REPR_data;
    if (repr_data) {
        MVM_serialization_write_varint(tc, writer, repr_data->primitive_type);
        MVM_serialization_write_varint(tc, writer, repr_data->ref_kind);
    }
    else {
        MVM_serialization_write_varint(tc, writer, 0);
        MVM_serialization_write_varint(tc, writer, 0);
    }
}

/* Deserializes REPR data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMNativeRefREPRData *repr_data = MVM_malloc(sizeof(MVMNativeRefREPRData));
    repr_data->primitive_type = MVM_serialization_read_varint(tc, reader);
    repr_data->ref_kind       = MVM_serialization_read_varint(tc, reader);
    st->REPR_data = repr_data;
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMNativeRefBody *ref = (MVMNativeRefBody *)data;
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)st->REPR_data;
    switch (repr_data->ref_kind) {
        case MVM_NATIVEREF_ATTRIBUTE:
            MVM_gc_worklist_add(tc, worklist, &ref->u.attribute.obj);
            MVM_gc_worklist_add(tc, worklist, &ref->u.attribute.class_handle);
            MVM_gc_worklist_add(tc, worklist, &ref->u.attribute.name);
            break;
        case MVM_NATIVEREF_POSITIONAL:
            MVM_gc_worklist_add(tc, worklist, &ref->u.positional.obj);
            break;
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMNativeRef *ref = (MVMNativeRef *)obj;
    MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(obj)->REPR_data;
    if (repr_data->ref_kind == MVM_NATIVEREF_LEXICAL)
        MVM_frame_dec_ref(tc, ref->body.u.lexical.frame);
}

/* Frees the representation data, if any. */
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_checked_free_null(st->REPR_data);
}

/* Gets the storage specification for this representation. */
static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMStringConsts *str_consts = &(tc->instance->str_consts);
    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_consts->nativeref);
    if (IS_CONCRETE(info)) {
        MVMObject *type    = MVM_repr_at_key_o(tc, info, str_consts->type);
        MVMuint16  prim    = REPR(type)->get_storage_spec(tc, STABLE(type))->boxed_primitive;
        if (prim != MVM_STORAGE_SPEC_BP_NONE) {
            MVMObject *refkind = MVM_repr_at_key_o(tc, info, str_consts->refkind);
            if (IS_CONCRETE(refkind)) {
                MVMNativeRefREPRData *repr_data;
                MVMuint16 kind;
                MVMString *refkind_s = MVM_repr_get_str(tc, refkind);
                if (MVM_string_equal(tc, refkind_s, str_consts->lexical)) {
                    kind = MVM_NATIVEREF_LEXICAL;
                }
                else if (MVM_string_equal(tc, refkind_s, str_consts->attribute)) {
                    kind = MVM_NATIVEREF_ATTRIBUTE;
                }
                else if (MVM_string_equal(tc, refkind_s, str_consts->positional)) {
                    kind = MVM_NATIVEREF_POSITIONAL;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "NativeRef: invalid refkind in compose");
                }

                repr_data = MVM_malloc(sizeof(MVMNativeRefREPRData));
                repr_data->primitive_type = prim;
                repr_data->ref_kind       = kind;
                st->REPR_data             = repr_data;
            }
            else {
                MVM_exception_throw_adhoc(tc, "NativeRef: missing refkind in compose");
            }
        }
        else {
            MVM_exception_throw_adhoc(tc, "NativeRef: non-native type supplied in compose");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "NativeRef: missing nativeref protocol in compose");
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMNativeRef_initialize(MVMThreadContext *tc) {
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
    NULL, /* serialize */
    NULL, /* deserialize */
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    gc_free_repr_data,
    compose,
    NULL, /* spesh */
    "NativeRef", /* name */
    MVM_REPR_ID_NativeRef,
    1, /* refs_frames */
};

/* Validates the given type is a native reference of the required primitive
 * type and reference kind. */
void MVM_nativeref_ensure(MVMThreadContext *tc, MVMObject *type, MVMuint16 wantprim, MVMuint16 wantkind, char *guilty) {
    if (REPR(type)->ID == MVM_REPR_ID_NativeRef) {
        MVMNativeRefREPRData *repr_data = (MVMNativeRefREPRData *)STABLE(type)->REPR_data;
        if (!repr_data)
            MVM_exception_throw_adhoc(tc, "%s set to NativeRef that is not yet composed", guilty);
        if (repr_data->primitive_type != wantprim)
            MVM_exception_throw_adhoc(tc, "%s set to NativeRef of wrong primitive type", guilty);
        if (repr_data->ref_kind != wantkind)
            MVM_exception_throw_adhoc(tc, "%s set to NativeRef of wrong reference kind", guilty);
    }
    else {
        MVM_exception_throw_adhoc(tc, "%s requires a type with REPR NativeRef", guilty);
    }
}

/* Creation of native references for attributes. */
static MVMObject * attrref(MVMThreadContext *tc, MVMObject *type, MVMObject *obj, MVMObject *class_handle, MVMString *name) {
    MVMNativeRef *ref;
    MVMROOT(tc, obj, {
    MVMROOT(tc, class_handle, {
    MVMROOT(tc, name, {
        ref = (MVMNativeRef *)MVM_gc_allocate_object(tc, STABLE(type));
        MVM_ASSIGN_REF(tc, &(ref->common.header), ref->body.u.attribute.obj, obj);
        MVM_ASSIGN_REF(tc, &(ref->common.header), ref->body.u.attribute.class_handle, class_handle);
        MVM_ASSIGN_REF(tc, &(ref->common.header), ref->body.u.attribute.name, name);
    });
    });
    });
    return (MVMObject *)ref;
}
MVMObject * MVM_nativeref_attr_i(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->int_attr_ref;
    if (ref_type)
        return attrref(tc, ref_type, obj, class_handle, name);
    MVM_exception_throw_adhoc(tc, "No int attribute reference type registered for current HLL");
}
MVMObject * MVM_nativeref_attr_n(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->num_attr_ref;
    if (ref_type)
        return attrref(tc, ref_type, obj, class_handle, name);
    MVM_exception_throw_adhoc(tc, "No num attribute reference type registered for current HLL");
}
MVMObject * MVM_nativeref_attr_s(MVMThreadContext *tc, MVMObject *obj, MVMObject *class_handle, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->str_attr_ref;
    if (ref_type)
        return attrref(tc, ref_type, obj, class_handle, name);
    MVM_exception_throw_adhoc(tc, "No str attribute reference type registered for current HLL");
}
