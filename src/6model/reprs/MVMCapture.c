#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMCapture_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMCapture_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCapture);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCaptureBody *src_body  = (MVMCaptureBody *)src;
    MVMCaptureBody *dest_body = (MVMCaptureBody *)dest;
    MVM_exception_throw_adhoc(tc, "Cannot clone an MVMCapture");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCaptureBody *body = (MVMCaptureBody *)data;
    MVMuint8 *flags = body->callsite->arg_flags;
    MVMuint16 count = body->callsite->flag_count;
    MVMuint16 i;
    for (i = 0; i < count; i++)
        if (flags[i] & MVM_CALLSITE_ARG_STR || flags[i] & MVM_CALLSITE_ARG_OBJ)
            MVM_gc_worklist_add(tc, worklist, &(body->args[i].o));
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCapture *capture = (MVMCapture *)obj;
    if (capture->body.args)
        MVM_fixed_size_free(tc, tc->instance->fsa,
                capture->body.callsite->flag_count * sizeof(MVMRegister),
                capture->body.args);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMCapture_initialize(MVMThreadContext *tc) {
    return &MVMCapture_this_repr;
}

static const MVMREPROps MVMCapture_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
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
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMCapture", /* name */
    MVM_REPR_ID_MVMCapture,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Form a capture object from an argument description. */
MVMObject * MVM_capture_from_args(MVMThreadContext *tc, MVMArgs arg_info) {
    /* Put callsite arguments into a flat buffer. */
    MVMCallsite *callsite = arg_info.callsite;
    MVMRegister *args = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            callsite->flag_count * sizeof(MVMRegister));
    MVMuint16 i;
    for (i = 0; i < callsite->flag_count; i++)
        args[i] = arg_info.source[arg_info.map[i]];

    /* Form capture object. */
    MVMObject *capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
    ((MVMCapture *)capture)->body.callsite = callsite;
    ((MVMCapture *)capture)->body.args = args;
    return capture;
}
