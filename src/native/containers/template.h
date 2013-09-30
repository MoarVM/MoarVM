#define CAN_BOX 0

#ifdef CAN_BOX_INT
#undef CAN_BOX
#define CAN_BOX MVM_STORAGE_SPEC_CAN_BOX_INT
#endif

#ifdef CAN_BOX_NUM
#undef CAN_BOX
#define CAN_BOX MVM_STORAGE_SPEC_CAN_BOX_NUM
#endif

#define EVAL(MACRO) DO_EVAL(MACRO, REPR_NAME, CTYPE, VMTYPE)
#define DO_EVAL(MACRO, ...) MACRO(__VA_ARGS__)

#define INIT(RN, CT, VT) MVM ## RN ## _initialize
#define NAME_STR(RN, CT, VT) #RN
#define REPR_ID(RN, CT, VT) MVM_REPR_ID_ ## RN
#define REPR_SET(RN, CT, VT) MVM_repr_set_ ## VT
#define REPR_GET(RN, CT, VT) MVM_repr_get_ ## VT
#define VMTYPE_REPR_ID(RN, CT, VT) MVM_REPR_ID_P6 ## VT
#define VMTYPE_CT(RN, CT, VT) MVM ## VT ## 64
#define CONFIGURER(RN, CT, VT) MVM_CONTAINER_CONF_ ## RN

#define ALIGNOF(type) \
    ((MVMuint16)offsetof(struct { char dummy; type member; }, member))

static const MVMContainerSpec container_spec;
static const MVMCScalarSpec scalar_spec;

static void set_container_spec(MVMThreadContext *tc, MVMSTable *st) {
    if (st->REPR->ID != MVM_REPR_ID_CScalar)
        MVM_exception_throw_adhoc(tc,
                "can only make C scalar objects into %s containers",
                EVAL(NAME_STR));

    st->container_spec = &container_spec;
    st->container_data = NULL;
}

static void configure_container_spec(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *config) {
    st->REPR_data = (void *)&scalar_spec;
}

const MVMContainerConfigurer EVAL(CONFIGURER) = {
    set_container_spec,
    configure_container_spec
};

static void gc_mark_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    /* nothing to mark */
}

static void fetch(MVMThreadContext *tc, MVMObject *cont, MVMRegister *res) {
    void *ptr = ((MVMPtr *)cont)->body.cobj;
    const MVMREPROps *repr = MVM_repr_get_by_id(tc, EVAL(VMTYPE_REPR_ID));
    MVMObject *type = repr->type_object_for(tc, NULL);
    MVMObject *box  = MVM_repr_alloc_init(tc, type);

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot fetch from null pointer");

    EVAL(REPR_SET)(tc, box, (EVAL(VMTYPE_CT))*(CTYPE *)ptr);
    res->o = box;
}

static void store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    MVMStorageSpec spec = REPR(obj)->get_storage_spec(tc, STABLE(obj));
    void *ptr = ((MVMPtr *)cont)->body.cobj;

    if (!(spec.can_box & CAN_BOX))
        MVM_exception_throw_adhoc(tc, "cannot unbox to required native type");

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot store into null pointer");

    *(CTYPE *)ptr = (CTYPE)EVAL(REPR_GET)(tc, obj);
}

static void store_unchecked(MVMThreadContext *tc, MVMObject *cont,
        MVMObject *obj) {
    void *ptr = ((MVMPtr *)cont)->body.cobj;

    *(CTYPE *)ptr = (CTYPE)EVAL(REPR_GET)(tc, obj);
}

static MVMint64 fetch_int(MVMThreadContext *tc, const void *storage) {
#ifdef CAN_BOX_INT
    return (MVMint64)*(CTYPE *)storage;
#else
    MVM_exception_throw_adhoc(tc, "cannot fetch int from " EVAL(NAME_STR));
#endif
}

static void store_int(MVMThreadContext *tc, void *storage, MVMint64 value) {
#ifdef CAN_BOX_INT
    *(CTYPE *)storage = (CTYPE)value;
#else
    MVM_exception_throw_adhoc(tc, "cannot store int into " EVAL(NAME_STR));
#endif

}

static MVMnum64 fetch_num(MVMThreadContext *tc, const void *storage) {
#ifdef CAN_BOX_NUM
    return (MVMnum64)*(CTYPE *)storage;
#else
    MVM_exception_throw_adhoc(tc, "cannot fetch num from " EVAL(NAME_STR));
#endif

}

static void store_num(MVMThreadContext *tc, void *storage, MVMnum64 value) {
#ifdef CAN_BOX_NUM
    *(CTYPE *)storage = (CTYPE)value;
#else
    MVM_exception_throw_adhoc(tc, "cannot store num into " EVAL(NAME_STR));
#endif
}

static const MVMContainerSpec container_spec = {
    EVAL(NAME_STR),
    fetch,
    store,
    store_unchecked,
    gc_mark_data,
    NULL, /* gc_free_data */
    NULL, /* serialize */
    NULL, /* deserialize */
};

static const MVMCScalarSpec scalar_spec = {
    sizeof(CTYPE),
    ALIGNOF(CTYPE),
    fetch_int,
    store_int,
    fetch_num,
    store_num,
};
