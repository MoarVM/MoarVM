#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(P6bigint);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    P6bigintBody *body = (P6bigintBody *)data;
    mp_init(&body->i);
    mp_zero(&body->i);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    P6bigintBody *src_body = (P6bigintBody *)src;
    P6bigintBody *dest_body = (P6bigintBody *)dest;
    mp_init_copy(&dest_body->i, &src_body->i);
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    mp_int *i = &((P6bigintBody *)data)->i;
    if (value >= 0) {
        mp_set_long(i, value);
    }
    else {
        mp_set_long(i, -value);
        mp_neg(i, i);
    }
}
static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMint64 ret;
    mp_int *i = &((P6bigintBody *)data)->i;
    if (MP_LT == mp_cmp_d(i, 0)) {
        mp_neg(i, i);
        ret = mp_get_long(i);
        mp_neg(i, i);
        return -ret;
    }
    else {
        return mp_get_long(i);
    }
}

static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot box a native num");
}
static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot unbox to a native num");
}
static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot box a native string");
}
static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot unbox to a native string");
}
static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    if (repr_id == MVM_REPR_ID_P6bigint)
        return &((P6bigintBody *)data)->i;

    MVM_exception_throw_adhoc(tc,
        "P6bigint representation cannot unbox to other types");
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_INLINED;
    spec.bits            = sizeof(mp_int) * 8;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_INT;
    spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
MVMREPROps * P6bigint_initialize(MVMThreadContext *tc) {
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->copy_to = copy_to;
    this_repr->get_storage_spec = get_storage_spec;
    this_repr->box_funcs = malloc(sizeof(MVMREPROps_Boxing));
    this_repr->box_funcs->set_int = set_int;
    this_repr->box_funcs->get_int = get_int;
    this_repr->box_funcs->set_num = set_num;
    this_repr->box_funcs->get_num = get_num;
    this_repr->box_funcs->set_str = set_str;
    this_repr->box_funcs->get_str = get_str;
    this_repr->box_funcs->get_boxed_ref = get_boxed_ref;
    this_repr->compose = compose;
    return this_repr;
}
