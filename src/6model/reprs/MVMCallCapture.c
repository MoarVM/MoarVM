#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMCallCapture_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMCallCapture_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCallCapture);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCallCaptureBody *src_body  = (MVMCallCaptureBody *)src;
    MVMCallCaptureBody *dest_body = (MVMCallCaptureBody *)dest;

    if (src_body->apc->version != MVM_ARGS_LEGACY)
        MVM_panic(1, "Should not be using legacy call capture with dispatch args");

    MVMuint32 arg_size = src_body->apc->legacy.arg_count * sizeof(MVMRegister);
    MVMRegister *args = MVM_malloc(arg_size);
    memcpy(args, src_body->apc->legacy.args, arg_size);

    dest_body->apc = (MVMArgProcContext *)MVM_calloc(1, sizeof(MVMArgProcContext));
    MVM_args_proc_init(tc, dest_body->apc,
        MVM_args_copy_uninterned_callsite(tc, src_body->apc), args);
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCallCaptureBody *body = (MVMCallCaptureBody *)data;
    MVMArgProcContext *ctx = body->apc;
    if (ctx->version != MVM_ARGS_LEGACY)
        MVM_panic(1, "Should not be using legacy call capture with dispatch args");
    MVMuint8  *flag_map = body->apc->legacy.callsite->arg_flags;
    MVMuint16  count = ctx->legacy.arg_count;
    MVMuint16  i, flag;
    for (i = 0, flag = 0; i < count; i++, flag++) {
        if (flag_map[flag] & MVM_CALLSITE_ARG_NAMED) {
            /* Current position is name, then next is value. */
            MVM_gc_worklist_add(tc, worklist, &ctx->legacy.args[i].s);
            i++;
        }
        if (flag_map[flag] & MVM_CALLSITE_ARG_STR || flag_map[flag] & MVM_CALLSITE_ARG_OBJ)
            MVM_gc_worklist_add(tc, worklist, &ctx->legacy.args[i].o);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCallCapture *ctx = (MVMCallCapture *)obj;
    /* We made our own copy of the callsite, args buffer and processing
     * context, so free them all. */
    if (ctx->body.apc) {
        if (ctx->body.apc->version != MVM_ARGS_LEGACY)
            MVM_panic(1, "Should not be using legacy call capture with dispatch args");
        MVMCallsite *cs = ctx->body.apc->legacy.callsite;
        if (cs && !cs->is_interned) {
            MVM_free(cs->arg_flags);
            MVM_free(cs);
        }
        if (ctx->body.apc->named_used_size > 64)
            MVM_fixed_size_free(tc, tc->instance->fsa,
                ctx->body.apc->named_used_size,
                ctx->body.apc->named_used.byte_array);
        MVM_free(ctx->body.apc->legacy.args);
        MVM_free(ctx->body.apc);
    }
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
const MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc) {
    return &MVMCallCapture_this_repr;
}

static const MVMREPROps MVMCallCapture_this_repr = {
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
    "MVMCallCapture", /* name */
    MVM_REPR_ID_MVMCallCapture,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* This function was only introduced for the benefit of the JIT. */
MVMint64 MVM_capture_pos_primspec(MVMThreadContext *tc, MVMObject *obj, MVMint64 i) {
    if (IS_CONCRETE(obj) && REPR(obj)->ID == MVM_REPR_ID_MVMCallCapture) {
        MVMCallCapture *cc = (MVMCallCapture *)obj;
        if (cc->body.apc->version != MVM_ARGS_LEGACY)
            MVM_panic(1, "Should not be using legacy call capture with dispatch args");
        if (i >= 0 && i < cc->body.apc->legacy.num_pos) {
            MVMCallsiteEntry *arg_flags = cc->body.apc->legacy.arg_flags
                ? cc->body.apc->legacy.arg_flags
                : cc->body.apc->legacy.callsite->arg_flags;
            switch (arg_flags[i] & MVM_CALLSITE_ARG_TYPE_MASK) {
                case MVM_CALLSITE_ARG_INT:
                    return MVM_STORAGE_SPEC_BP_INT;
                case MVM_CALLSITE_ARG_NUM:
                    return MVM_STORAGE_SPEC_BP_NUM;
                case MVM_CALLSITE_ARG_STR:
                    return MVM_STORAGE_SPEC_BP_STR;
                default:
                    return MVM_STORAGE_SPEC_BP_NONE;
            }
        }
        else {
            MVM_exception_throw_adhoc(tc,
                "Bad argument index given to captureposprimspec");
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "captureposprimspec needs a MVMCallCapture");
    }
}
