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
    if (!body->callsite->is_interned)
        MVM_callsite_mark(tc, body->callsite, worklist);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCapture *capture = (MVMCapture *)obj;
    if (capture->body.args)
        MVM_fixed_size_free(tc, tc->instance->fsa,
                capture->body.callsite->flag_count * sizeof(MVMRegister),
                capture->body.args);
    if (capture->body.callsite && !capture->body.callsite->is_interned)
        MVM_callsite_destroy(capture->body.callsite);
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

/* Form a capture object from an argument description. If the callsite is not an
 * interned one, then it will be copied, since an MVMCapture assumes that it
 * owns a non-interned callsite. */
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
    ((MVMCapture *)capture)->body.callsite = callsite->is_interned
        ? callsite
        : MVM_callsite_copy(tc, callsite);
    ((MVMCapture *)capture)->body.args = args;
    return capture;
}

static MVMCapture * validate_capture(MVMThreadContext *tc, MVMObject *capture) {
    if (REPR(capture)->ID != MVM_REPR_ID_MVMCapture)
        MVM_exception_throw_adhoc(tc, "Capture operation requires an MVMCapture");
    if (!IS_CONCRETE(capture))
        MVM_exception_throw_adhoc(tc, "Capture operation requires concreate capture object");
    return (MVMCapture *)capture;
}

/* Access a positional object argument of an argument capture object. */
MVMObject * MVM_capture_arg_pos_o(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Capture argument is not an object argument");
    return capture->body.args[idx].o;
}

/* Access a positional string argument of an argument capture object. */
MVMString * MVM_capture_arg_pos_s(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_STR)
        MVM_exception_throw_adhoc(tc, "Capture argument is not a string argument");
    return capture->body.args[idx].s;
}

/* Access a positional integer argument of an argument capture object. */
MVMint64 MVM_capture_arg_pos_i(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_INT)
        MVM_exception_throw_adhoc(tc, "Capture argument is not an integer argument");
    return capture->body.args[idx].i64;
}

/* Access a positional number argument of an argument capture object. */
MVMnum64 MVM_capture_arg_pos_n(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_STR)
        MVM_exception_throw_adhoc(tc, "Capture argument is not a number argument");
    return capture->body.args[idx].n64;
}

/* Obtain a positional argument's value and type together. */
void MVM_capture_arg_pos(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");
    *arg_out = capture->body.args[idx];
    *arg_type_out = capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK;
}

/* Produce a new capture by taking the current one and dropping the specified
 * positional argument from it. */
MVMObject * MVM_capture_drop_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");

    /* We need a callsite without the argument that is being dropped. */
    MVMCallsite *new_callsite = MVM_callsite_drop_positional(tc, capture->body.callsite, idx);

    /* Form a new arguments buffer, dropping the specified argument. */
    MVMRegister *new_args = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            new_callsite->flag_count * sizeof(MVMRegister));
    MVMuint32 from, to = 0;
    for (from = 0; from < capture->body.callsite->flag_count; from++) {
        if (from != idx) {
            new_args[to] = capture->body.args[from];
            to++;
        }
    }

    /* Form new capture object. */
    MVMObject *new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
    ((MVMCapture *)new_capture)->body.callsite = new_callsite;
    ((MVMCapture *)new_capture)->body.args = new_args;
    return new_capture;
}

/* Produce a new capture by taking the current one and inserting the specified
 * arg into it. */
MVMObject * MVM_capture_insert_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMCallsiteFlags kind, MVMRegister value) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx > capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");

    /* We need a callsite with the argument that is being inserted. */
    MVMCallsite *new_callsite = MVM_callsite_insert_positional(tc, capture->body.callsite,
            idx, kind);

    /* Form a new arguments buffer, dropping the specified argument. */
    MVMRegister *new_args = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            new_callsite->flag_count * sizeof(MVMRegister));
    MVMuint32 from, to = 0;
    for (from = 0; from < capture->body.callsite->flag_count; from++) {
        if (from == idx) {
            new_args[to] = value;
            to++;
        }
        new_args[to] = capture->body.args[from];
        to++;
    }
    if (from == idx)
        new_args[to] = value;

    /* Form new capture object. */
    MVMObject *new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
    ((MVMCapture *)new_capture)->body.callsite = new_callsite;
    ((MVMCapture *)new_capture)->body.args = new_args;
    return new_capture;
}
