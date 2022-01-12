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
    dest_body->callsite = src_body->callsite->is_interned
        ? src_body->callsite
        : MVM_callsite_copy(tc, src_body->callsite);
    size_t arg_size = dest_body->callsite->flag_count * sizeof(MVMRegister);
    if (arg_size) {
        dest_body->args = MVM_malloc(arg_size);
        memcpy(dest_body->args, src_body->args, arg_size);
    }
    else {
        dest_body->args = NULL;
    }
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
        MVM_callsite_mark(tc, body->callsite, worklist, NULL);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCapture *capture = (MVMCapture *)obj;
    if (capture->body.args)
        MVM_free(capture->body.args);
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
    /* Allocate capture before we begin, otherwise we might end up with outdated
     * pointeres in args. */
    MVMObject *capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);

    /* Put callsite arguments into a flat buffer. */
    MVMCallsite *callsite = arg_info.callsite;
    MVMRegister *args;
    if (callsite->flag_count) {
        args = MVM_malloc(callsite->flag_count * sizeof(MVMRegister));
        MVMuint16 i;
        for (i = 0; i < callsite->flag_count; i++)
            args[i] = arg_info.source[arg_info.map[i]];
    }
    else {
        args = NULL;
    }

    /* Form capture object. */
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

/* Extract an MVMArgs structure from the capture. One must ensure the capture
 * lives for the entire lifetime of the MVMArgs that are populated. */
MVMArgs MVM_capture_to_args(MVMThreadContext *tc, MVMObject *capture_obj) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    MVMArgs args;
    args.callsite = capture->body.callsite;
    args.source = capture->body.args;
    args.map = MVM_args_identity_map(tc, args.callsite);
    return args;
}

/* Get the number of positional arguments that the capture has. */
MVMint64 MVM_capture_num_pos_args(MVMThreadContext *tc, MVMObject *capture_obj) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    return capture->body.callsite->num_pos;
}

/* Get the number of arguments (positional and named) that the capture has. */
MVMint64 MVM_capture_num_args(MVMThreadContext *tc, MVMObject *capture_obj) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    return capture->body.callsite->flag_count;
}

/* Get the primitive value kind for a positional argument. */
static MVMint64 flag_to_spec(MVMint64 flag) {
    switch (flag & MVM_CALLSITE_ARG_TYPE_MASK) {
        case MVM_CALLSITE_ARG_INT:
            return MVM_STORAGE_SPEC_BP_INT;
        case MVM_CALLSITE_ARG_UINT:
            return MVM_STORAGE_SPEC_BP_UINT64;
        case MVM_CALLSITE_ARG_NUM:
            return MVM_STORAGE_SPEC_BP_NUM;
        case MVM_CALLSITE_ARG_STR:
            return MVM_STORAGE_SPEC_BP_STR;
        default:
            return MVM_STORAGE_SPEC_BP_NONE;
    }
}
MVMint64 MVM_capture_arg_pos_primspec(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposprimspec", idx, capture->body.callsite->num_pos);
    return flag_to_spec(capture->body.callsite->arg_flags[idx]);
}

/* Get the primitive value kind for an argument. */
MVMint64 MVM_capture_arg_primspec(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u)", idx, capture->body.callsite->flag_count);
    return flag_to_spec(capture->body.callsite->arg_flags[idx]);
}

/* Access a positional object argument of an argument capture object. */
MVMObject * MVM_capture_arg_pos_o(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposarg", idx, capture->body.callsite->num_pos);
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Capture argument is not an object argument for captureposarg. Got %d instead",
            (capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK));
    return capture->body.args[idx].o;
}

/* Access an object argument of an argument capture object. */
MVMObject * MVM_capture_arg_o(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u)", idx, capture->body.callsite->flag_count);
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_OBJ)
        MVM_exception_throw_adhoc(tc, "Capture argument is not an object argument");
    return capture->body.args[idx].o;
}

/* Access a positional string argument of an argument capture object. */
MVMString * MVM_capture_arg_pos_s(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposarg_s", idx, capture->body.callsite->num_pos);
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_STR)
        MVM_exception_throw_adhoc(tc, "Capture argument is not a string argument for captureposarg_s");
    return capture->body.args[idx].s;
}

/* Access a positional integer argument of an argument capture object. */
MVMint64 MVM_capture_arg_pos_i(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposarg_i", idx, capture->body.callsite->num_pos);
    if (!(capture->body.callsite->arg_flags[idx] & (MVM_CALLSITE_ARG_INT | MVM_CALLSITE_ARG_UINT)))
        MVM_exception_throw_adhoc(tc, "Capture argument is not an integer argument for captureposarg_i");
    return capture->body.args[idx].i64;
}

/* Access a positional unsigned integer argument of an argument capture object. */
MVMuint64 MVM_capture_arg_pos_u(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposarg_u", idx, capture->body.callsite->num_pos);
    if (!(capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_INT))
        MVM_exception_throw_adhoc(tc, "Capture argument is not an integer argument for captureposarg_u");
    return capture->body.args[idx].u64;
}

/* Access a positional number argument of an argument capture object. */
MVMnum64 MVM_capture_arg_pos_n(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u) for captureposarg_n", idx, capture->body.callsite->num_pos);
    if ((capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != MVM_CALLSITE_ARG_NUM)
        MVM_exception_throw_adhoc(tc, "Capture argument is not a number argument for captureposarg_n");
    return capture->body.args[idx].n64;
}

/* Obtain a positional argument's value and type together. */
void MVM_capture_arg_pos(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u)", idx, capture->body.callsite->num_pos);
    *arg_out = capture->body.args[idx];
    *arg_type_out = capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK;
}

/* Obtain an argument's value and type together. */
void MVM_capture_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..^%u)", idx, capture->body.callsite->flag_count);
    *arg_out = capture->body.args[idx];
    *arg_type_out = capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK;
}

/* Checks if the capture has a named arg with the specified name. */
MVMint64 MVM_capture_has_named_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMString *name) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    MVMCallsite *cs = capture->body.callsite;
    MVMuint32 num_nameds = cs->flag_count - cs->num_pos;
    MVMuint32 i;
    for (i = 0; i < num_nameds; i++)
        if (MVM_string_equal(tc, cs->arg_names[i], name))
            return 1;
    return 0;
}

/* Checks if the capture has nameds at all. */
MVMint64 MVM_capture_has_nameds(MVMThreadContext *tc, MVMObject *capture_obj) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    MVMCallsite *cs = capture->body.callsite;
    return cs->flag_count != cs->num_pos;
}

/* Gets a string array of the nameds that the capture has. Evaluates to a type
 * object if there are no nameds. */
MVMObject * MVM_capture_get_names_list(MVMThreadContext *tc, MVMObject *capture_obj) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    MVMCallsite *cs = capture->body.callsite;
    MVMuint16 num_nameds = cs->flag_count - cs->num_pos;
    if (num_nameds == 0)
        return tc->instance->boot_types.BOOTStrArray;
    MVMObject *result = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTStrArray);
    for (MVMuint16 i = 0; i < num_nameds; i++)
        MVM_repr_bind_pos_s(tc, result, i, cs->arg_names[i]);
    return result;
}

/* Get a hash of the named arguments. */
MVMObject * MVM_capture_get_nameds(MVMThreadContext *tc, MVMObject *capture) {
    /* Set up an args processing context and use the standard slurpy args
     * handler to extract all nameds */
    MVMObject *result;
    MVMROOT(tc, capture, {
        MVMArgs capture_args = MVM_capture_to_args(tc, capture);
        MVMArgProcContext capture_ctx;
        MVM_args_proc_setup(tc, &capture_ctx, capture_args);
        result = MVM_args_slurpy_named(tc, &capture_ctx);
        MVM_args_proc_cleanup(tc, &capture_ctx);
    });
    return result;
}

/* Obtain an argument by its flag index (that is, positional arguments have
 * their positions, and then names are according the order that the names
 * appear in in the callsite's argument name list). */
void MVM_capture_arg_by_flag_index(MVMThreadContext *tc, MVMObject *capture_obj,
        MVMuint32 idx, MVMRegister *arg_out, MVMCallsiteFlags *arg_type_out) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx >= capture->body.callsite->flag_count)
        MVM_exception_throw_adhoc(tc, "Capture argument flag index (%u) out of range (0..^%u)", idx, capture->body.callsite->flag_count);
    *arg_out = capture->body.args[idx];
    *arg_type_out = capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK;
}

/* Check if the argument at the given position is marked as literal. */
MVMint64 MVM_capture_is_literal_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    return (capture->body.callsite->arg_flags[idx] & MVM_CALLSITE_ARG_LITERAL) ? 1 : 0;
}

/* Produce a new capture by taking the current one and dropping the specified
 * positional argument from it. */
MVMObject * MVM_capture_drop_args(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx, MVMuint32 count) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx + count > capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index (%u) out of range (0..%u)", idx + count, capture->body.callsite->num_pos);

    /* Allocate a new capture before we begin; this is the only GC allocation
     * we do. */
    MVMObject *new_capture;
    MVMROOT(tc, capture, {
        new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
    });

    /* We need a callsite without the arguments that are being dropped. */
    MVMCallsite *new_callsite = MVM_callsite_drop_positionals(tc, capture->body.callsite, idx, count);

    /* Form a new arguments buffer, dropping the specified argument. */
    MVMRegister *new_args;
    if (new_callsite->flag_count) {
        new_args = MVM_malloc(new_callsite->flag_count * sizeof(MVMRegister));
        MVMuint32 from, to = 0;
        for (from = 0; from < capture->body.callsite->flag_count; from++) {
            if (from < idx || from >= idx + count) {
                new_args[to] = capture->body.args[from];
                to++;
            }
        }
    }
    else {
        new_args = NULL;
    }

    /* Form new capture object. */
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

    /* Allocate a new capture before we begin; this is the only GC allocation
     * we do. */
    MVMObject *new_capture;
    MVMROOT(tc, capture, {
        if (kind & (MVM_CALLSITE_ARG_OBJ | MVM_CALLSITE_ARG_STR)) {
            MVMROOT(tc, value.o, {
                new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
            });
        }
        else {
            new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
        }
    });

    /* We need a callsite with the argument that is being inserted. */
    MVMCallsite *new_callsite = MVM_callsite_insert_positional(tc, capture->body.callsite,
            idx, kind);

    /* Form a new arguments buffer, dropping the specified argument. */
    MVMRegister *new_args = MVM_malloc(new_callsite->flag_count * sizeof(MVMRegister));
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
    ((MVMCapture *)new_capture)->body.callsite = new_callsite;
    ((MVMCapture *)new_capture)->body.args = new_args;
    return new_capture;
}

/* Produce a new capture by taking the current one and replacing a designated
 * argument with a new value.
 *
 * The callsite argument type is expected to be the same. */
MVMObject * MVM_capture_replace_arg(MVMThreadContext *tc, MVMObject *capture_obj, MVMuint32 idx,
        MVMCallsiteEntry kind, MVMRegister value) {
    MVMCapture *capture = validate_capture(tc, capture_obj);
    if (idx > capture->body.callsite->num_pos)
        MVM_exception_throw_adhoc(tc, "Capture argument index out of range");

    /* Allocate a new capture before we begin; this is the only GC allocation
     * we do. */
    MVMObject *new_capture;
    MVMROOT(tc, capture, {
        if (kind & (MVM_CALLSITE_ARG_OBJ | MVM_CALLSITE_ARG_STR)) {
            MVMROOT(tc, value.o, {
                new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
            });
        }
        else {
            new_capture = MVM_repr_alloc(tc, tc->instance->boot_types.BOOTCapture);
        }
    });

    /* We need a new callsite with the argument flag replaced.
     * The callsite MUST be created after we allocated as it may contain named
     * arguments, i.e. contain pointers to strings which wouldn't get marked. */
    MVMCallsite *callsite = capture->body.callsite;
    if ((callsite->arg_flags[idx] & MVM_CALLSITE_ARG_TYPE_MASK) != kind)
        MVM_exception_throw_adhoc(tc, "Cannot replace capture argument with different kind %d -> %d", callsite->arg_flags[idx], kind);

    MVMCallsite *new_callsite = MVM_callsite_replace_positional(tc, callsite, idx, kind);
    new_callsite->arg_flags[idx] = kind;

    /* Form a new arguments buffer, replacing the specified argument. */
    MVMRegister *new_args = MVM_malloc(callsite->flag_count * sizeof(MVMRegister));
    MVMuint32 from = 0;
    for (from = 0; from < capture->body.callsite->flag_count; from++) {
        new_args[from] = capture->body.args[from];
    }
    new_args[idx] = value;

    /* Form new capture object. */
    ((MVMCapture *)new_capture)->body.callsite = new_callsite;
    ((MVMCapture *)new_capture)->body.args = new_args;
    return new_capture;
}
