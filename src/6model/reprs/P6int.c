#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Some strings. */
static MVMString *str_integer  = NULL;
static MVMString *str_bits     = NULL;
static MVMString *str_unsigned = NULL;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVMP6intREPRData *repr_data = (MVMP6intREPRData *)malloc(sizeof(MVMP6intREPRData));

        repr_data->bits = sizeof(MVMint64) * 8;
        repr_data->is_unsigned = 0;

        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6int);
        st->REPR_data = repr_data;
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6intBody *src_body  = (MVMP6intBody *)src;
    MVMP6intBody *dest_body = (MVMP6intBody *)dest;
    dest_body->value = src_body->value;
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    ((MVMP6intBody *)data)->value = value;
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    return ((MVMP6intBody *)data)->value;
}

/* Marks the representation data in an STable.*/
static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_checked_free_null(st->REPR_data);
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
    spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;

    if (repr_data && repr_data->bits)
        spec.bits = repr_data->bits;
    else
        spec.bits = sizeof(MVMint64) * 8;
    if (repr_data && repr_data->is_unsigned)
        spec.is_unsigned = 1;
    else
        spec.is_unsigned = 0;

    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info_hash) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;

    MVMObject *info = MVM_repr_at_key_o(tc, info_hash, str_integer);
    if (info != NULL) {
        MVMObject *bits_o        = MVM_repr_at_key_o(tc, info, str_bits);
        MVMObject *is_unsigned_o = MVM_repr_at_key_o(tc, info, str_unsigned);

        if (bits_o != NULL) {
            repr_data->bits = MVM_repr_get_int(tc, bits_o);
            if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
             && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
                MVM_exception_throw_adhoc(tc, "MVMP6int: Unsupported int size (%dbit)", repr_data->bits);
        }

        if (is_unsigned_o != NULL) {
            repr_data->is_unsigned = MVM_repr_get_int(tc, is_unsigned_o);
        }
    }
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6int);
}

/* Serializes the REPR data. */
static void serialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationWriter *writer) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)st->REPR_data;
    writer->write_varint(tc, writer, repr_data->bits);
    writer->write_varint(tc, writer, repr_data->is_unsigned);
}

/* Deserializes representation data. */
static void deserialize_repr_data(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    MVMP6intREPRData *repr_data = (MVMP6intREPRData *)malloc(sizeof(MVMP6intREPRData));

    if (reader->root.version >= 8) {
        if (reader->root.version >= 9) {
            repr_data->bits        = reader->read_varint(tc, reader);
            repr_data->is_unsigned = reader->read_varint(tc, reader);
        } else {
            repr_data->bits        = reader->read_int16(tc, reader);
            repr_data->is_unsigned = reader->read_int16(tc, reader);
        }
        if (repr_data->bits !=  1 && repr_data->bits !=  2 && repr_data->bits !=  4 && repr_data->bits != 8
         && repr_data->bits != 16 && repr_data->bits != 32 && repr_data->bits != 64)
            MVM_exception_throw_adhoc(tc, "MVMP6int: Unsupported int size (%dbit)", repr_data->bits);
    }
    else {
        repr_data->bits        = sizeof(MVMint64) * 8;
        repr_data->is_unsigned = 0;
    }

    st->REPR_data = repr_data;
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    ((MVMP6intBody *)data)->value = reader->read_varint(tc, reader);
}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    writer->write_varint(tc, writer, ((MVMP6intBody *)data)->value);
}

/* Initializes the representation. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc) {
    /* Set up some constant strings we'll need. */
    str_integer = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "integer");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_integer);
    str_bits = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "bits");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_bits);
    str_unsigned = MVM_string_ascii_decode_nt(tc, tc->instance->VMString, "unsigned");
    MVM_gc_root_add_permanent(tc, (MVMCollectable **)&str_unsigned);

    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
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
    "P6int", /* name */
    MVM_REPR_ID_P6int,
    0, /* refs_frames */
};
