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
        case MVM_NATIVEREF_REG_OR_LEX:
            MVM_gc_worklist_add_frame(tc, worklist, ref->u.reg_or_lex.frame);
            break;
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
    if (repr_data->ref_kind == MVM_NATIVEREF_REG_OR_LEX)
        MVM_frame_dec_ref(tc, ref->body.u.reg_or_lex.frame);
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
                    kind = MVM_NATIVEREF_REG_OR_LEX;
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

/* Creation of native references for registers. */
static MVMObject * reg_or_lex_ref(MVMThreadContext *tc, MVMObject *type, MVMFrame *f, MVMRegister *r, MVMuint16 reg_type) {
    MVMNativeRef *ref = (MVMNativeRef *)MVM_gc_allocate_object(tc, STABLE(type));
    ref->body.u.reg_or_lex.frame = MVM_frame_inc_ref(tc, f);
    ref->body.u.reg_or_lex.var   = r;
    ref->body.u.reg_or_lex.type  = reg_type;
    return (MVMObject *)ref;
}
MVMObject * MVM_nativeref_reg_i(MVMThreadContext *tc, MVMFrame *f, MVMRegister *r) {
    MVMObject *ref_type = MVM_hll_current(tc)->int_lex_ref;
    if (ref_type)
        return reg_or_lex_ref(tc, ref_type, f, r, MVM_reg_int64);
    MVM_exception_throw_adhoc(tc, "No int lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_reg_n(MVMThreadContext *tc, MVMFrame *f, MVMRegister *r) {
    MVMObject *ref_type = MVM_hll_current(tc)->num_lex_ref;
    if (ref_type)
        return reg_or_lex_ref(tc, ref_type, f, r, MVM_reg_num64);
    MVM_exception_throw_adhoc(tc, "No num lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_reg_s(MVMThreadContext *tc, MVMFrame *f, MVMRegister *r) {
    MVMObject *ref_type = MVM_hll_current(tc)->str_lex_ref;
    if (ref_type)
        return reg_or_lex_ref(tc, ref_type, f, r, MVM_reg_str);
    MVM_exception_throw_adhoc(tc, "No str lexical reference type registered for current HLL");
}

/* Creation of native references for lexicals. */
static MVMFrame * get_lexical_outer(MVMThreadContext *tc, MVMuint16 outers) {
    MVMFrame *f = tc->cur_frame;
    while (outers) {
        if (!f)
            MVM_exception_throw_adhoc(tc, "getlexref_*: outer index out of range");
        f = f->outer;
        outers--;
    }
    return f;
}
MVMObject * MVM_nativeref_lex_i(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->int_lex_ref;
    if (ref_type) {
        MVMFrame  *f = get_lexical_outer(tc, outers);
        MVMuint16 *lexical_types = f->spesh_cand && f->spesh_cand->lexical_types
            ? f->spesh_cand->lexical_types
            : f->static_info->body.lexical_types;
        MVMuint16 type = lexical_types[idx];
        if (type != MVM_reg_int64 && type != MVM_reg_int32 &&
                type != MVM_reg_int16 && type != MVM_reg_int8)
            MVM_exception_throw_adhoc(tc, "getlexref_i: lexical is not an int");
        return reg_or_lex_ref(tc, ref_type, f, &(f->env[idx]), type);
    }
    MVM_exception_throw_adhoc(tc, "No int lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_lex_n(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->num_lex_ref;
    if (ref_type) {
        MVMFrame  *f = get_lexical_outer(tc, outers);
        MVMuint16 *lexical_types = f->spesh_cand && f->spesh_cand->lexical_types
            ? f->spesh_cand->lexical_types
            : f->static_info->body.lexical_types;
        MVMuint16 type = lexical_types[idx];
        if (type != MVM_reg_num64 && type != MVM_reg_num32)
            MVM_exception_throw_adhoc(tc, "getlexref_n: lexical is not a num");
        return reg_or_lex_ref(tc, ref_type, f, &(f->env[idx]), type);
    }
    MVM_exception_throw_adhoc(tc, "No num lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_lex_s(MVMThreadContext *tc, MVMuint16 outers, MVMuint16 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->str_lex_ref;
    if (ref_type) {
        MVMFrame  *f = get_lexical_outer(tc, outers);
        MVMuint16 *lexical_types = f->spesh_cand && f->spesh_cand->lexical_types
            ? f->spesh_cand->lexical_types
            : f->static_info->body.lexical_types;
        if (lexical_types[idx] != MVM_reg_str)
            MVM_exception_throw_adhoc(tc, "getlexref_s: lexical is not a str (%d, %d)", outers, idx);
        return reg_or_lex_ref(tc, ref_type, f, &(f->env[idx]), MVM_reg_str);
    }
    MVM_exception_throw_adhoc(tc, "No str lexical reference type registered for current HLL");
}
static MVMObject * lexref_by_name(MVMThreadContext *tc, MVMObject *type, MVMString *name, MVMuint16 kind) {
    MVMFrame *cur_frame = tc->cur_frame;
    MVM_string_flatten(tc, name);
    while (cur_frame != NULL) {
        MVMLexicalRegistry *lexical_names = cur_frame->static_info->body.lexical_names;
        if (lexical_names) {
            MVMLexicalRegistry *entry;
            MVM_HASH_GET(tc, lexical_names, name, entry)
            if (entry) {
                if (cur_frame->static_info->body.lexical_types[entry->value] == kind) {
                    return reg_or_lex_ref(tc, type, cur_frame, &cur_frame->env[entry->value], kind);
                }
                else {
                   MVM_exception_throw_adhoc(tc,
                        "Lexical with name '%s' has wrong type",
                            MVM_string_utf8_encode_C_string(tc, name));
                }
            }
        }
        cur_frame = cur_frame->outer;
    }
    MVM_exception_throw_adhoc(tc, "No lexical found with name '%s'",
        MVM_string_utf8_encode_C_string(tc, name));
}
MVMObject * MVM_nativeref_lex_name_i(MVMThreadContext *tc, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->int_lex_ref;
    if (ref_type)
        return lexref_by_name(tc, ref_type, name, MVM_reg_int64);
    MVM_exception_throw_adhoc(tc, "No int lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_lex_name_n(MVMThreadContext *tc, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->num_lex_ref;
    if (ref_type)
        return lexref_by_name(tc, ref_type, name, MVM_reg_num64);
    MVM_exception_throw_adhoc(tc, "No num lexical reference type registered for current HLL");
}
MVMObject * MVM_nativeref_lex_name_s(MVMThreadContext *tc, MVMString *name) {
    MVMObject *ref_type = MVM_hll_current(tc)->str_lex_ref;
    if (ref_type)
        return lexref_by_name(tc, ref_type, name, MVM_reg_str);
    MVM_exception_throw_adhoc(tc, "No str lexical reference type registered for current HLL");
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

/* Creation of native references for positionals. */
static MVMObject * posref(MVMThreadContext *tc, MVMObject *type, MVMObject *obj, MVMint64 idx) {
    MVMNativeRef *ref;
    MVMROOT(tc, obj, {
        ref = (MVMNativeRef *)MVM_gc_allocate_object(tc, STABLE(type));
        MVM_ASSIGN_REF(tc, &(ref->common.header), ref->body.u.positional.obj, obj);
        ref->body.u.positional.idx = idx;
    });
    return (MVMObject *)ref;
}
MVMObject * MVM_nativeref_pos_i(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->int_pos_ref;
    if (ref_type)
        return posref(tc, ref_type, obj, idx);
    MVM_exception_throw_adhoc(tc, "No int positional reference type registered for current HLL");
}
MVMObject * MVM_nativeref_pos_n(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->num_pos_ref;
    if (ref_type)
        return posref(tc, ref_type, obj, idx);
    MVM_exception_throw_adhoc(tc, "No num positional reference type registered for current HLL");
}
MVMObject * MVM_nativeref_pos_s(MVMThreadContext *tc, MVMObject *obj, MVMint64 idx) {
    MVMObject *ref_type = MVM_hll_current(tc)->str_pos_ref;
    if (ref_type)
        return posref(tc, ref_type, obj, idx);
    MVM_exception_throw_adhoc(tc, "No str positional reference type registered for current HLL");
}

/* Reference read functions. These do no checks that the reference is of the
 * right kind and primitive type, they just go ahead and do the read. Thus
 * they are more suited to calling from optimized code. The checking path is
 * in the native ref container implementation, in containers.c; after checks,
 * they delegate here. */
MVMint64 MVM_nativeref_read_reg_or_lex_i(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    switch (ref->body.u.reg_or_lex.type) {
        case MVM_reg_int8:
            return ref->body.u.reg_or_lex.var->i8;
        case MVM_reg_int16:
            return ref->body.u.reg_or_lex.var->i16;
        case MVM_reg_int32:
            return ref->body.u.reg_or_lex.var->i32;
        default:
            return ref->body.u.reg_or_lex.var->i64;
    }
}
MVMnum64 MVM_nativeref_read_reg_or_lex_n(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    switch (ref->body.u.reg_or_lex.type) {
        case MVM_reg_num32:
            return ref->body.u.reg_or_lex.var->n32;
        default:
            return ref->body.u.reg_or_lex.var->n64;
    }
}
MVMString * MVM_nativeref_read_reg_or_lex_s(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return ref->body.u.reg_or_lex.var->s;
}
MVMint64 MVM_nativeref_read_attribute_i(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_get_attr_i(tc, ref->body.u.attribute.obj,
        ref->body.u.attribute.class_handle, ref->body.u.attribute.name, MVM_NO_HINT);
}
MVMnum64 MVM_nativeref_read_attribute_n(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_get_attr_n(tc, ref->body.u.attribute.obj,
        ref->body.u.attribute.class_handle, ref->body.u.attribute.name, MVM_NO_HINT);
}
MVMString * MVM_nativeref_read_attribute_s(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_get_attr_s(tc, ref->body.u.attribute.obj,
        ref->body.u.attribute.class_handle, ref->body.u.attribute.name, MVM_NO_HINT);
}
MVMint64 MVM_nativeref_read_positional_i(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_at_pos_i(tc, ref->body.u.positional.obj, ref->body.u.positional.idx);
}
MVMnum64 MVM_nativeref_read_positional_n(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_at_pos_n(tc, ref->body.u.positional.obj, ref->body.u.positional.idx);
}
MVMString * MVM_nativeref_read_positional_s(MVMThreadContext *tc, MVMObject *ref_obj) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    return MVM_repr_at_pos_s(tc, ref->body.u.positional.obj, ref->body.u.positional.idx);
}

/* Reference write functions. Same (non-checking) rules as the reads above. */
void MVM_nativeref_write_reg_or_lex_i(MVMThreadContext *tc, MVMObject *ref_obj, MVMint64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    switch (ref->body.u.reg_or_lex.type) {
        case MVM_reg_int8:
            ref->body.u.reg_or_lex.var->i8 = (MVMint8)value;
            break;
        case MVM_reg_int16:
            ref->body.u.reg_or_lex.var->i16 = (MVMint16)value;
            break;
        case MVM_reg_int32:
            ref->body.u.reg_or_lex.var->i32 = (MVMint32)value;
            break;
        default:
            ref->body.u.reg_or_lex.var->i64 = value;
            break;
    }
}
void MVM_nativeref_write_reg_or_lex_n(MVMThreadContext *tc, MVMObject *ref_obj, MVMnum64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    switch (ref->body.u.reg_or_lex.type) {
        case MVM_reg_num32:
            ref->body.u.reg_or_lex.var->n32 = (MVMnum32)value;
            break;
        default:
            ref->body.u.reg_or_lex.var->n64 = value;
            break;
    }
}
void MVM_nativeref_write_reg_or_lex_s(MVMThreadContext *tc, MVMObject *ref_obj, MVMString *value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    ref->body.u.reg_or_lex.var->s = value;
}
void MVM_nativeref_write_attribute_i(MVMThreadContext *tc, MVMObject *ref_obj, MVMint64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVMRegister r;
    r.i64 = value;
    MVM_repr_bind_attr_inso(tc, ref->body.u.attribute.obj, ref->body.u.attribute.class_handle,
        ref->body.u.attribute.name, MVM_NO_HINT, r, MVM_reg_int64);
}
void MVM_nativeref_write_attribute_n(MVMThreadContext *tc, MVMObject *ref_obj, MVMnum64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVMRegister r;
    r.n64 = value;
    MVM_repr_bind_attr_inso(tc, ref->body.u.attribute.obj, ref->body.u.attribute.class_handle,
        ref->body.u.attribute.name, MVM_NO_HINT, r, MVM_reg_num64);
}
void MVM_nativeref_write_attribute_s(MVMThreadContext *tc, MVMObject *ref_obj, MVMString *value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVMRegister r;
    r.s = value;
    MVM_repr_bind_attr_inso(tc, ref->body.u.attribute.obj, ref->body.u.attribute.class_handle,
        ref->body.u.attribute.name, MVM_NO_HINT, r, MVM_reg_str);
}
void MVM_nativeref_write_positional_i(MVMThreadContext *tc, MVMObject *ref_obj, MVMint64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVM_repr_bind_pos_i(tc, ref->body.u.positional.obj, ref->body.u.positional.idx, value);
}
void MVM_nativeref_write_positional_n(MVMThreadContext *tc, MVMObject *ref_obj, MVMnum64 value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVM_repr_bind_pos_n(tc, ref->body.u.positional.obj, ref->body.u.positional.idx, value);
}
void MVM_nativeref_write_positional_s(MVMThreadContext *tc, MVMObject *ref_obj, MVMString *value) {
    MVMNativeRef *ref = (MVMNativeRef *)ref_obj;
    MVM_repr_bind_pos_s(tc, ref->body.u.positional.obj, ref->body.u.positional.idx, value);
}
