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
        st->size = sizeof(MVMCStr);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* TODO: move encoding stuff into here */
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCPointerBody *src_body = (MVMCPointerBody *)src;
    MVMCPointerBody *dest_body = (MVMCPointerBody *)dest;
    dest_body->ptr = src_body->ptr;
}

static MVMCallsiteEntry obj_arg_flags[] = { MVM_CALLSITE_ARG_OBJ };
static MVMCallsite     inv_arg_callsite = { obj_arg_flags, 1, 1, 0 };

static void set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVMCStrBody *body = (MVMCStrBody *) data;
    MVMObject   *code;
    MVMRegister  res;

    /* Look up "encoding" method. */
    MVMObject *encoding_method = MVM_6model_find_method_cache_only(tc, st->WHAT,
        tc->instance->str_consts.encoding);

    if(body->cstr)
        free(body->cstr);

    if (!encoding_method)
        MVM_exception_throw_adhoc(tc, "CStr representation expects an 'encoding' method, specifying the encoding");

    /* We need to do the invocation; just set it up with our result reg as
     * the one for the call. */
    code = MVM_frame_find_invokee(tc, encoding_method, NULL);
    MVM_args_setup_thunk(tc, &res, MVM_CALLSITE_ARG_OBJ, &inv_arg_callsite);
    tc->cur_frame->args[0].o = st->WHAT;
    STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);

    MVMROOT(tc, value, {
        MVMuint64 output_size;
        const MVMuint8 encoding_flag = MVM_string_find_encoding(tc, MVM_repr_get_str(tc, res.o));
        body->cstr = MVM_string_encode(tc, value, 0, NUM_GRAPHS(value), &output_size,
            encoding_flag);
    });
}

static MVMString * get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCStrBody *body = (MVMCStrBody *) data;
    MVMObject   *encoding_method;
    MVMObject   *code;
    MVMRegister  res;
    MVMuint8 encoding_flag;

    if (!body->cstr)
        return NULL;

    /* Look up "encoding" method. */
    encoding_method = MVM_6model_find_method_cache_only(tc, st->WHAT,
        tc->instance->str_consts.encoding);

    if (!encoding_method)
        MVM_exception_throw_adhoc(tc, "CStr representation expects an 'encoding' method, specifying the encoding");

    /* We need to do the invocation; just set it up with our result reg as
     * the one for the call. */
    code = MVM_frame_find_invokee(tc, encoding_method, NULL);
    MVM_args_setup_thunk(tc, &res, MVM_CALLSITE_ARG_OBJ, &inv_arg_callsite);
    tc->cur_frame->args[0].o = st->WHAT;
    STABLE(code)->invoke(tc, code, &inv_arg_callsite, tc->cur_frame->args);

    encoding_flag = MVM_string_find_encoding(tc, MVM_repr_get_str(tc, res.o));

    return MVM_string_decode(tc, tc->instance->VMString, body->cstr, strlen(body->cstr), encoding_flag);
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_STR;
    spec.can_box = MVM_STORAGE_SPEC_CAN_BOX_STR;
    spec.bits = sizeof(void *) * 8;
    return spec;
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCStrBody *body = (MVMCStrBody *) obj;

    if(obj && body->cstr)
        free(body->cstr);
}

/* Initializes the representation. */
const MVMREPROps * MVMCStr_initialize(MVMThreadContext *tc) {
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
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        set_str,
        get_str,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
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
    NULL, /* gc_mark */
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "CStr", /* name */
    MVM_REPR_ID_MVMCStr,
    0, /* refs_frames */
};