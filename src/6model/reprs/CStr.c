#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps CStr_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &CStr_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject       *obj       = MVM_gc_allocate_type_object(tc, st);
        MVMCStrREPRData *repr_data = MVM_malloc(sizeof(MVMCStrREPRData));
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;

        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size      = sizeof(MVMCStr);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCStrBody     *src_body  = (MVMCStrBody *)src;
    MVMCStrBody     *dest_body = (MVMCStrBody *)dest;
    MVMCStrREPRData *repr_data = (MVMCStrREPRData *)st->REPR_data;

    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->source, src_body->source);

    switch (repr_data->type) {
        case MVM_P6STR_C_TYPE_CHAR:     dest_body->value.c    = src_body->value.c;    break;
        case MVM_P6STR_C_TYPE_WCHAR_T:  dest_body->value.wide = src_body->value.wide; break;
        case MVM_P6STR_C_TYPE_CHAR16_T: dest_body->value.u16  = src_body->value.u16;  break;
        case MVM_P6STR_C_TYPE_CHAR32_T: dest_body->value.u32  = src_body->value.u32;  break;
    }
}

static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVMCStrBody     *body      = (MVMCStrBody *)data;
    MVMCStrREPRData *repr_data = (MVMCStrREPRData *)st->REPR_data;

    MVM_ASSIGN_REF(tc, &(root->header), body->source, value);

    switch (repr_data->type) {
        case MVM_P6STR_C_TYPE_CHAR:
            body->value.c = MVM_string_utf8_encode_C_string(tc, value);
            break;
        case MVM_P6STR_C_TYPE_WCHAR_T: {
            MVMwchar  *wide;
            mbstate_t  state;
            size_t     len;
            size_t     els;

            char *c = MVM_string_utf8_encode_C_string(tc, value);
            memcpy(&state, 0, sizeof(mbstate_t));
            len = mbsrtowcs(NULL, (const char **)&c, 0, &state);
            els = mbsrtowcs(wide, (const char **)&c, len, &state);
            if (els == (size_t)-1)
                MVM_exception_throw_adhoc(tc, "CStr: failed to encode wide string with error '%s'", strerror(errno));

            body->value.wide = wide;
            MVM_free(c);
            break;
        }
        case MVM_P6STR_C_TYPE_CHAR16_T:
            MVM_exception_throw_adhoc(tc, "CStr: u16string support NYI");
            break;
        case MVM_P6STR_C_TYPE_CHAR32_T:
            MVM_exception_throw_adhoc(tc, "CStr: u32string support NYI");
            break;
    }
}

static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCStrBody *body = (MVMCStrBody *)data;
    return body->source;
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

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVM_serialization_write_str(tc, writer, get_str(tc, st, NULL, data));
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    set_str(tc, st, root, data, MVM_serialization_read_str(tc, reader));
}

static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMCStrREPRData *repr_data = (MVMCStrREPRData *)st->REPR_data;
    MVM_serialization_write_int(tc, writer, repr_data->type);
}

static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMCStrREPRData *repr_data = MVM_malloc(sizeof(MVMCStrREPRData));

    if (reader->root.version >= 22) {
        repr_data->type = MVM_serialization_read_int(tc, reader);
    } else {
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;
    }

    if (repr_data->type != MVM_P6STR_C_TYPE_CHAR && repr_data->type != MVM_P6STR_C_TYPE_WCHAR_T
     && repr_data->type != MVM_P6STR_C_TYPE_CHAR16_T && repr_data->type != MVM_P6STR_C_TYPE_CHAR32_T)
        MVM_exception_throw_adhoc(tc, "CStr: unsupported character type (%d)", repr_data->type);

    st->REPR_data = repr_data;
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCStr);
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCStrBody *body = (MVMCStrBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->source);
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCStrBody     *body      = (MVMCStrBody *)OBJECT_BODY(obj);
    MVMCStrREPRData *repr_data = (MVMCStrREPRData *)STABLE(obj)->REPR_data;
    switch (repr_data->type) {
        case MVM_P6STR_C_TYPE_CHAR:     MVM_free(body->value.c);    break;
        case MVM_P6STR_C_TYPE_WCHAR_T:  MVM_free(body->value.wide); break;
        case MVM_P6STR_C_TYPE_CHAR16_T: MVM_free(body->value.u16);  break;
        case MVM_P6STR_C_TYPE_CHAR32_T: MVM_free(body->value.u32);  break;
    }
}

static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_free(st->REPR_data);
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMCStrREPRData *repr_data  = (MVMCStrREPRData *)st->REPR_data;
    MVMStringConsts  str_consts = tc->instance->str_consts;
    MVMObject       *info       = MVM_repr_at_key_o(tc, info_hash, str_consts.string);

    if (!MVM_is_null(tc, info)) {
        MVMObject *type_o = MVM_repr_at_key_o(tc, info, str_consts.type);
        repr_data->type = MVM_repr_get_int(tc, type_o);
    }
    else {
        repr_data->type = MVM_P6STR_C_TYPE_CHAR;
    }
}

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCStrBody     *body      = (MVMCStrBody *)data;
    MVMCStrREPRData *repr_data = (MVMCStrREPRData *)st->REPR_data;
    switch (repr_data->type) {
        case MVM_P6STR_C_TYPE_CHAR:
            return sizeof(char) * (strlen(body->value.c) + 1);
        case MVM_P6STR_C_TYPE_WCHAR_T:
            return sizeof(MVMwchar) * (wcslen(body->value.wide) + 1);
        case MVM_P6STR_C_TYPE_CHAR16_T:
            MVM_exception_throw_adhoc(tc, "CStr: u16string support NYI");
            break;
        case MVM_P6STR_C_TYPE_CHAR32_T:
            MVM_exception_throw_adhoc(tc, "CStr: u32string support NYI");
            break;
        default:
            MVM_exception_throw_adhoc(tc, "CStr: unsupported native type (%d)", repr_data->type);
            break;
    }
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
    NULL, /* change type */
    serialize,
    deserialize,
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
    "CStr",
    MVM_REPR_ID_MVMCStr,
    unmanaged_size,
    NULL, /* describe_refs */
};
