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

static const MVMREPROps this_repr;
static const MVMContainerSpec container_spec;

const MVMREPROps * EVAL(INIT)(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMPtr);
        st->container_spec = &container_spec;
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVMPtrBody *body = data;

    body->cobj = NULL;
    body->blob = NULL;
}

static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src,
        MVMObject *dest_root, void *dest) {
    MVMPtrBody *src_body  = src;
    MVMPtrBody *dest_body = dest;

    dest_body->cobj = src_body->cobj;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->blob, src_body->blob);
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 value) {
#ifdef CAN_BOX_INT
    *(CTYPE *)((MVMPtrBody *)data)->cobj = (CTYPE)value;
#else
    MVM_exception_throw_adhoc(tc, "cannot set int into " EVAL(NAME_STR));
#endif
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
#ifdef CAN_BOX_INT
    return (MVMint64)*(CTYPE *)((MVMPtrBody *)data)->cobj;
#else
    MVM_exception_throw_adhoc(tc, "cannot get int from " EVAL(NAME_STR));
#endif
}

static void set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMnum64 value) {
#ifdef CAN_BOX_NUM
    *(CTYPE *)((MVMPtrBody *)data)->cobj = (CTYPE)value;
#else
    MVM_exception_throw_adhoc(tc, "cannot set num into " EVAL(NAME_STR));
#endif
}

static MVMnum64 get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
#ifdef CAN_BOX_NUM
    return (MVMnum64)*(CTYPE *)((MVMPtrBody *)data)->cobj;
#else
    MVM_exception_throw_adhoc(tc, "cannot get num from " EVAL(NAME_STR));
#endif
}

static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data,  MVMString *value) {
    MVM_exception_throw_adhoc(tc, "cannot set str into " EVAL(NAME_STR));
}

static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVM_exception_throw_adhoc(tc, "cannot get str from " EVAL(NAME_STR));
}

static void * get_boxed_ref(MVMThreadContext *tc, MVMSTable *st,
        MVMObject *root, void *data, MVMuint32 repr_id) {
    MVM_exception_throw_adhoc(tc, "cannot get boxed ref from " EVAL(NAME_STR));
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = CAN_BOX;

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMPtrBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* noop */
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    { /* box_funcs */
        set_int,
        get_int,
        set_num,
        get_num,
        set_str,
        get_str,
        get_boxed_ref
    },
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    EVAL(NAME_STR),
    EVAL(REPR_ID),
    0, /* refs_frames */
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

static const MVMContainerSpec container_spec = {
    NULL, /* name */
    fetch,
    store,
    store_unchecked,
    gc_mark_data,
    NULL, /* gc_free_data */
    NULL, /* serialize */
    NULL, /* deserialize */
};
