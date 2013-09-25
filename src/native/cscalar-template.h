#include "moarvm.h"

#define EVAL(MACRO) DO_EVAL(MACRO, REPR_NAME)
#define DO_EVAL(MACRO, ...) MACRO(__VA_ARGS__)

#define INIT(RN) MVM ## RN ## _initialize
#define NAME_STR(RN) #RN
#define REPR_ID(RN) MVM_REPR_ID_ ## RN
#define CAN_BOX(RN) RN ## _CAN_BOX
#define FETCH_BOXED(RN) RN ## _FETCH_BOXED
#define STORE_BOXED(RN) RN ## _STORE_BOXED

#define CChar_CAN_BOX           MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUChar_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CShort_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUShort_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CInt_CAN_BOX            MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUInt_CAN_BOX           MVM_STORAGE_SPEC_CAN_BOX_INT
#define CLong_CAN_BOX           MVM_STORAGE_SPEC_CAN_BOX_INT
#define CULong_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CLLong_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CULLong_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CInt8_CAN_BOX           MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUInt8_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CInt16_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUInt16_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CInt32_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUInt32_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CInt64_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUInt64_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CIntPtr_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUIntPtr_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define CIntMax_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_INT
#define CUIntMax_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_INT
#define CFloat_CAN_BOX          MVM_STORAGE_SPEC_CAN_BOX_NUM
#define CDouble_CAN_BOX         MVM_STORAGE_SPEC_CAN_BOX_NUM
#define CLDouble_CAN_BOX        MVM_STORAGE_SPEC_CAN_BOX_NUM

#define CChar_FETCH_BOXED       DO_FETCH_BOXED(signed char, int)
#define CUChar_FETCH_BOXED      DO_FETCH_BOXED(unsigned char, int)
#define CShort_FETCH_BOXED      DO_FETCH_BOXED(short, int)
#define CUShort_FETCH_BOXED     DO_FETCH_BOXED(unsigned short, int)
#define CInt_FETCH_BOXED        DO_FETCH_BOXED(int, int)
#define CUInt_FETCH_BOXED       DO_FETCH_BOXED(unsigned int, int)
#define CLong_FETCH_BOXED       DO_FETCH_BOXED(long, int)
#define CULong_FETCH_BOXED      DO_FETCH_BOXED(unsigned long, int)
#define CLLong_FETCH_BOXED      DO_FETCH_BOXED(long long, int)
#define CULLong_FETCH_BOXED     DO_FETCH_BOXED(unsigned long long, int)
#define CInt8_FETCH_BOXED       DO_FETCH_BOXED(int8_t, int)
#define CUInt8_FETCH_BOXED      DO_FETCH_BOXED(uint8_t, int)
#define CInt16_FETCH_BOXED      DO_FETCH_BOXED(int16_t, int)
#define CUInt16_FETCH_BOXED     DO_FETCH_BOXED(uint16_t, int)
#define CInt32_FETCH_BOXED      DO_FETCH_BOXED(int32_t, int)
#define CUInt32_FETCH_BOXED     DO_FETCH_BOXED(uint32_t, int)
#define CInt64_FETCH_BOXED      DO_FETCH_BOXED(int64_t, int)
#define CUInt64_FETCH_BOXED     DO_FETCH_BOXED(uint64_t, int)
#define CIntPtr_FETCH_BOXED     DO_FETCH_BOXED(intptr_t, int)
#define CUIntPtr_FETCH_BOXED    DO_FETCH_BOXED(uintptr_t, int)
#define CIntMax_FETCH_BOXED     DO_FETCH_BOXED(intmax_t, int)
#define CUIntMax_FETCH_BOXED    DO_FETCH_BOXED(uintmax_t, int)
#define CFloat_FETCH_BOXED      DO_FETCH_BOXED(float, num)
#define CDouble_FETCH_BOXED     DO_FETCH_BOXED(double, num)
#define CLDouble_FETCH_BOXED    DO_FETCH_BOXED(long double, num)

#define CChar_STORE_BOXED       DO_STORE_BOXED(signed char, int)
#define CUChar_STORE_BOXED      DO_STORE_BOXED(unsigned char, int)
#define CShort_STORE_BOXED      DO_STORE_BOXED(short, int)
#define CUShort_STORE_BOXED     DO_STORE_BOXED(unsigned short, int)
#define CInt_STORE_BOXED        DO_STORE_BOXED(int, int)
#define CUInt_STORE_BOXED       DO_STORE_BOXED(unsigned int, int)
#define CLong_STORE_BOXED       DO_STORE_BOXED(long, int)
#define CULong_STORE_BOXED      DO_STORE_BOXED(unsigned long, int)
#define CLLong_STORE_BOXED      DO_STORE_BOXED(long long, int)
#define CULLong_STORE_BOXED     DO_STORE_BOXED(unsigned long long, int)
#define CInt8_STORE_BOXED       DO_STORE_BOXED(int8_t, int)
#define CUInt8_STORE_BOXED      DO_STORE_BOXED(uint8_t, int)
#define CInt16_STORE_BOXED      DO_STORE_BOXED(int16_t, int)
#define CUInt16_STORE_BOXED     DO_STORE_BOXED(uint16_t, int)
#define CInt32_STORE_BOXED      DO_STORE_BOXED(int32_t, int)
#define CUInt32_STORE_BOXED     DO_STORE_BOXED(uint32_t, int)
#define CInt64_STORE_BOXED      DO_STORE_BOXED(int64_t, int)
#define CUInt64_STORE_BOXED     DO_STORE_BOXED(uint64_t, int)
#define CIntPtr_STORE_BOXED     DO_STORE_BOXED(intptr_t, int)
#define CUIntPtr_STORE_BOXED    DO_STORE_BOXED(uintptr_t, int)
#define CIntMax_STORE_BOXED     DO_STORE_BOXED(intmax_t, int)
#define CUIntMax_STORE_BOXED    DO_STORE_BOXED(uintmax_t, int)
#define CFloat_STORE_BOXED      DO_STORE_BOXED(float, num)
#define CDouble_STORE_BOXED     DO_STORE_BOXED(double, num)
#define CLDouble_STORE_BOXED    DO_STORE_BOXED(long double, num)

#define DO_FETCH_NATIVE(CTYPE, VMTYPE, REGMEMBER) { \
    res->REGMEMBER = (MVM ## VMTYPE ## 64)*(CTYPE *)ptr; \
}

#define DO_FETCH_BOXED(CTYPE, VMTYPE) { \
    CTYPE value = *(CTYPE *)ptr; \
    const MVMREPROps *repr = MVM_repr_get_by_id(tc, MVM_REPR_ID_P6 ## VMTYPE); \
    MVMObject *type = repr->type_object_for(tc, NULL); \
    MVMObject *box  = MVM_repr_alloc_init(tc, type); \
    MVM_repr_set_ ## VMTYPE(tc, box, (MVM ## VMTYPE ## 64)value); \
    res->o = box; \
}

#define DO_STORE_NATIVE(CTYPE, VMTYPE, REGMEMBER) { \
    *(CTYPE *)ptr = (CTYPE)reg.REGMEMBER; \
}

#define DO_STORE_BOXED(CTYPE, VMTYPE) { \
    *(CTYPE *)ptr = (CTYPE)MVM_repr_get_ ## VMTYPE(tc, reg.o); \
}

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

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = EVAL(CAN_BOX);

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
    MVM_REPR_DEFAULT_BOX_FUNCS,
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

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot fetch from null pointer");

    EVAL(FETCH_BOXED)
}

static void store(MVMThreadContext *tc, MVMObject *cont, MVMObject *obj) {
    MVMStorageSpec spec = REPR(obj)->get_storage_spec(tc, STABLE(obj));
    void *ptr = ((MVMPtr *)cont)->body.cobj;
    MVMRegister reg;

    if (!(spec.can_box & EVAL(CAN_BOX)))
        MVM_exception_throw_adhoc(tc, "cannot unbox to required native type");

    if (!ptr)
        MVM_exception_throw_adhoc(tc, "cannot store into null pointer");

    reg.o = obj;
    EVAL(STORE_BOXED)
}

static void store_unchecked(MVMThreadContext *tc, MVMObject *cont,
        MVMObject *obj) {
    void *ptr = ((MVMPtr *)cont)->body.cobj;
    MVMRegister reg;

    reg.o = obj;
    EVAL(STORE_BOXED)
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
