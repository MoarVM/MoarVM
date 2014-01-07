#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Some strings. */
static MVMString *str_float = NULL;
static MVMString *str_bits  = NULL;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMP6numREPRData *repr_data = (MVMP6numREPRData *)malloc(sizeof(MVMP6numREPRData));

        repr_data->bits = sizeof(MVMnum64) * 8;

        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMP6num);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6numBody *src_body  = (MVMP6numBody *)src;
    MVMP6numBody *dest_body = (MVMP6numBody *)dest;
    dest_body->value = src_body->value;
}

static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    ((MVMP6numBody *)data)->value = value;
}

static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    return ((MVMP6numBody *)data)->value;
}

/* Marks the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_checked_free_null(st->REPR_data);
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NUM;
    spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_NUM;

    if (repr_data && repr_data->bits) {
        spec.bits = repr_data->bits;
        if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
         && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
            MVM_exception_throw_adhoc(tc, "MVMP6num: Unsupported int size (%d bit)", repr_data->bits);
    }
    else {
        spec.bits = sizeof(MVMnum64) * 8;
    }

    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;

    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_float);
    if (info != NULL) {
        MVMObject *bits_o = MVM_repr_at_key_o(tc, info, str_bits);

        if (bits_o != NULL) {
            repr_data->bits = MVM_repr_get_int(tc, bits_o);
            if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
             && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
                MVM_exception_throw_adhoc(tc, "MVMP6num: Unsupported int size (%dbit)", repr_data->bits);
        }
    }
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6num);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)st->REPR_data;
    writer->write_int16(tc, writer, repr_data->bits);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMP6numREPRData *repr_data = (MVMP6numREPRData *)malloc(sizeof(MVMP6numREPRData));

    if (reader->root.version >= 8) {
        repr_data->bits        = reader->read_int16(tc, reader);
        if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
         && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
            MVM_exception_throw_adhoc(tc, "MVMP6num: Unsupported int size (%dbit)", repr_data->bits);
    }
    else {
        repr_data->bits = sizeof(MVMnum64) * 8;
    }

    st->REPR_data = repr_data;
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    ((MVMP6numBody *)data)->value = reader->read_num(tc, reader);
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    writer->write_num(tc, writer, ((MVMP6numBody *)data)->value);
}

/* Initializes the representation. */
const MVMREPROps * MVMP6num_initialize(MVMThreadContext *tc) {
    /* Set up some constant strings we'll need. */
    str_float = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "float");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_float);
    str_bits = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "bits");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_bits);

    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        MVM_REPR_DEFAULT_SET_INT,
        MVM_REPR_DEFAULT_GET_INT,
        set_num,
        get_num,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize,
    serialize_repr_data,
    deserialize_repr_data,
    deserialize_stable_size,
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    gc_free_repr_data,
    compose,
    "P6num", /* name */
    MVM_REPR_ID_P6num,
    0, /* refs_frames */
};
