#include "moar.h"

#ifndef MIN
   #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/* Get a native int64 from an mpz_t. */
static MVMint64 mp_get_int64(MVMThreadContext *tc, mpz_t * a) {
#ifdef _MSC_VER
    if (mpz_size(*a) < 2) {
        MVMuint64 ui = mpz_getlimbn(*a, 0);
        int negative = mpz_sgn(*a) == -1;
        if ((negative && ui > 9223372036854775808LLU) || (!negative && ui > 9223372036854775807LLU)) {
            MVM_exception_throw_adhoc(tc, "Cannot unbox %lu bit wide bigint into native integer", mpz_sizeinbase(*a, 2));
        }
        else {
            return negative ? -ui : ui;
        }
#else
    if (mpz_fits_slong_p(*a)) {
        return mpz_get_si(*a);
#endif
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot unbox %lu bit wide bigint into native integer", mpz_sizeinbase(*a, 2));
    }
}

MVMint64 MVM_p6bigint_get_int64(MVMThreadContext *tc, MVMP6bigintBody *body) {
    return mp_get_int64(tc, body->u.bigint);
}

/* Get a native uint64 from an mpz_t. */
static MVMuint64 mp_get_uint64(MVMThreadContext *tc, mpz_t * a) {
#ifdef _MSC_VER
    if (mpz_size(*a) < 2) {
        return mpz_getlimbn(*a, 0);
#else
    if (mpz_fits_ulong_p(*a)) {
        return mpz_get_ui(*a);
#endif
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot unbox %lu bit wide bigint into native integer", mpz_sizeinbase(*a, 2));
    }
}

/* This representation's function pointer table. */
static const MVMREPROps P6bigint_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &P6bigint_this_repr, HOW);

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
        dest_body->u.bigint = MVM_malloc(sizeof(mpz_t));
        mpz_init_set(*dest_body->u.bigint, *src_body->u.bigint);
    }
    else {
        dest_body->u.smallint.flag = src_body->u.smallint.flag;
        dest_body->u.smallint.value = src_body->u.smallint.value;
    }
}

void MVM_p6bigint_store_as_mpz_t(MVMThreadContext *tc, MVMP6bigintBody *body, MVMint64 value) {
    mpz_t *i = MVM_malloc(sizeof(mpz_t));
#ifdef _MSC_VER
    mpz_init(*i);
    MVMuint64 a = llabs(value);
    mpz_import(*i, 1, 1, sizeof(value), 0, 0, &a);
    if (value < 0)
	mpz_neg(*i, *i);
#else
    mpz_init_set_si(*i, value);
#endif
    body->u.bigint = i;
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_IS_32BIT_INT(value)) {
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = (MVMint32)value;
    }
    else {
        MVM_p6bigint_store_as_mpz_t(tc, body, value);
    }
}
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mpz_t *i = body->u.bigint;
        return mp_get_int64(tc, i);
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
        mpz_t *i = MVM_malloc(sizeof(mpz_t));
#ifdef _MSC_VER
        mpz_init(*i);
        mp_limb_t *l = mpz_limbs_write(*i, 1L);
        l[0] = value;
        mpz_limbs_finish(*i, 1L);
#else
        mpz_init_set_ui(*i, value);
#endif
        body->u.bigint = i;
    }
}
static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mpz_t *i = body->u.bigint;
        if (mpz_sgn(*i) < 0)
            MVM_exception_throw_adhoc(tc, "Cannot unbox negative bigint into native unsigned integer");
        else
            return mp_get_uint64(tc, i);
    }
    else {
        return body->u.smallint.value;
    }
}

static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    if (repr_id == MVM_REPR_ID_P6bigint)
        return data;

    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot unbox to representation %s", MVM_repr_get_by_id(tc, repr_id)->name);
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
        mpz_clear(*body->u.bigint);
        MVM_free(body->u.bigint);
    }
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMP6bigintBody *body = &((MVMP6bigint *)obj)->body;
    if (MVM_BIGINT_IS_BIG(body)) {
        mpz_clear(*body->u.bigint);
        MVM_free(body->u.bigint);
    }
}

/* Serializes the bigint. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body)) {
        mpz_t *i = body->u.bigint;
        char *buf = mpz_get_str(NULL, 10, *i);
        int len = strlen(buf);
        MVMString *str = MVM_string_ascii_decode(tc, tc->instance->VMString, buf, len);

        /* write the "is small" flag */
        MVM_serialization_write_int(tc, writer, 0);
        MVM_serialization_write_str(tc, writer, str);
        MVM_free(buf);
    }
    else {
        /* write the "is small" flag */
        MVM_serialization_write_int(tc, writer, 1);
        MVM_serialization_write_int(tc, writer, body->u.smallint.value);
    }
}

/* Set the size on the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMP6bigint);
}

/* Deserializes the bigint. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;

    if (MVM_serialization_read_int(tc, reader) == 1) { /* Is it small int? */
        body->u.smallint.flag = MVM_BIGINT_32_FLAG;
        body->u.smallint.value = MVM_serialization_read_int(tc, reader);
    } else {  /* big int */
        char *buf = MVM_string_ascii_encode_any(tc, MVM_serialization_read_str(tc, reader));
        body->u.bigint = MVM_malloc(sizeof(mpz_t));
        int err = mpz_init_set_str(*body->u.bigint, buf, 10);
        if (err == -1) {
            mpz_clear(*body->u.bigint);
            MVM_free(body->u.bigint);
            MVM_free(buf);
            MVM_exception_throw_adhoc(tc, "Error converting a string to a big integer");
        }
        MVM_free(buf);
    }
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMP6bigintBody *body = (MVMP6bigintBody *)data;
    if (MVM_BIGINT_IS_BIG(body))
        return mpz_size(*body->u.bigint) * sizeof(mp_limb_t);
    else
        return 0;
}

/* Initializes the representation. */
const MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc) {
    return &P6bigint_this_repr;
}

static const MVMREPROps P6bigint_this_repr = {
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
    unmanaged_size, /* unmanaged_size */
    NULL, /* describe_refs */
};
