#include "moar.h"

#ifndef MIN
   #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/* A forced 64-bit version of mp_get_long, since on some platforms long is
 * not all that long. */
static MVMuint64 mp_get_int64(MVMThreadContext *tc, mp_int * a) {
    int i, bits;
    MVMuint64 res;

    if (a->used == 0) {
         return 0;
    }

    bits = mp_count_bits(a);
    if (bits > 64) {
        MVM_exception_throw_adhoc(tc, "Cannot unbox %d bit wide bigint into native integer", bits);
    }

    /* get number of digits of the lsb we have to read */
    i = MIN(a->used,(int)((sizeof(MVMuint64)*CHAR_BIT+DIGIT_BIT-1)/DIGIT_BIT))-1;

    /* get most significant digit of result */
    res = DIGIT(a,i);

    while (--i >= 0) {
        res = (res << DIGIT_BIT) | DIGIT(a,i);
    }
    return res;
}

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMP6bigint);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    body->u.smallint.flag = MVM_BIGINT_32_FLAG;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMP6bigintBody *src_body = (MVMP6bigintBody *)src;
    MVMP6bigintBody *dest_body = (MVMP6bigintBody *)dest;
    if (MVM_BIGINT_IS_BIG(src_body)) {
        dest_body->u.bigint = MVM_malloc(sizeof(mp_int));
        mp_init_copy(dest_body->u.bigint, src_body->u.bigint);
    }
    else {
        dest_body->u.smallint.flag = src_body->u.smallint.flag;
        dest_body->u.smallint.value = src_body->u.smallint.value;
    }
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_IS_32BIT_INT(value)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)value;
    }
    else {
        mp_int *i = MVM_malloc(sizeof(mp_int));
        mp_init(i);
        if (value >= 0) {
            MVM_bigint_mp_set_uint64(i, (MVMuint64)value);
        }
        else {
            MVM_bigint_mp_set_uint64(i, (MVMuint64)-value);
            mp_neg(i, i);
        }
        body->u.bigint = i;
    }
}
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_int *i = body->u.bigint;
        if (MP_LT == mp_cmp_d(i, 0)) {
            MVMint64 ret;
            mp_neg(i, i);
            ret = mp_get_int64(tc, i);
            mp_neg(i, i);
            return -ret;
        }
        else {
            return mp_get_int64(tc, i);
        }
    }
    else {
        return body->u.smallint.value;
    }
}

static void set_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (value < 2147483647ULL) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)value;
    }
    else {
        mp_int *i = MVM_malloc(sizeof(mp_int));
        mp_init(i);
        MVM_bigint_mp_set_uint64(i, value);
        body->u.bigint = i;
    }
}
static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_int *i = body->u.bigint;
        if (MP_LT == mp_cmp_d(i, 0))
            MVM_exception_throw_adhoc(tc, "Cannot unbox negative bigint into native unsigned integer");
        else
            return mp_get_int64(tc, i);
    }
    else {
        return body->u.smallint.value;
    }
}

static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    if (repr_id == MVM_REPR_ID_P6bigint)
        return data;

    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot unbox to other types");
}


static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_INLINED,      /* inlineable */
    sizeof(MVMP6bigintBody) * 8,   /* bits */
    ALIGNOF(MVMP6bigintBody),      /* align */
    MVM_STORAGE_SPEC_BP_INT,       /* boxed_primitive */
    MVM_STORAGE_SPEC_CAN_BOX_INT,  /* can_box */
    0,                             /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

static void gc_cleanup(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_clear(body->u.bigint);
        MVM_free(body->u.bigint);
    }
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMP6bigintBody *body = &((MVMP6bigint *)obj)->body;
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_clear(body->u.bigint);
        MVM_free(body->u.bigint);
    }
}

/* Serializes the bigint. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mp_int *i = body->u.bigint;
        int len;
        char *buf;
        MVMString *str;
        mp_radix_size(i, 10, &len);
        buf = (char *)MVM_malloc(len);
        mp_toradix(i, buf, 10);

        /* len - 1 because buf is \0-terminated */
        str = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len - 1);

        /* write the "is small" flag */
        MVM_serialization_write_varint(tc, writer, 0);
        MVM_serialization_write_str(tc, writer, str);
        MVM_free(buf);
    }
    else {
        /* write the "is small" flag */
        MVM_serialization_write_varint(tc, writer, 1);
        MVM_serialization_write_varint(tc, writer, body->u.smallint.value);
    }
}

/* Set the size on the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6bigint);
}

/* Deserializes the bigint. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;

    if (MVM_serialization_read_varint(tc, reader) == 1) { /* Is it small int? */
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = MVM_serialization_read_varint(tc, reader);
    } else {  /* big int */
        char *buf = MVM_string_ascii_encode(tc, MVM_serialization_read_str(tc, reader), NULL, 0);
        body->u.bigint = MVM_malloc(sizeof(mp_int));
        mp_init(body->u.bigint);
        mp_read_radix(body->u.bigint, buf, 10);
        MVM_free(buf);
    }
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body))
        return body->u.bigint->alloc;
    else
        return 0;
}

/* Initializes the representation. */
const MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        set_uint,
        get_uint,
        get_boxed_ref
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
    NULL, /* gc_mark */
    gc_free,
    gc_cleanup,
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "P6bigint", /* name */
    MVM_REPR_ID_P6bigint,
    0, /* refs_frames */
    unmanaged_size, /* unmanaged_size */
    NULL, /* describe_refs */
};
