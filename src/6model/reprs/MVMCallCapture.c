#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMCallCapture);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_panic(MVM_exitcode_NYI, "MVMCallCapture cannot be copied");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    /* Only need to worry about the SAVE case, as the USE case will be marked by
     * the frame holding the args being marked. */
    MVMCallCaptureBody *body = (MVMCallCaptureBody *)data;
    if (body->mode == MVM_CALL_CAPTURE_MODE_SAVE) {
        MVMArgProcContext *ctx = body->apc;
        MVMuint8  *flag_map = ctx->arg_flags ? ctx->arg_flags : ctx->callsite->arg_flags;
        MVMuint16  count = ctx->arg_count;
        MVMuint16  i, flag;
        for (i = 0, flag = 0; i < count; i++, flag++) {
            if (flag_map[flag] & MVM_CALLSITE_ARG_NAMED) {
                /* Current position is name, then next is value. */
                MVM_gc_worklist_add(tc, worklist, &ctx->args[i].s);
                i++;
            }
            if (flag_map[flag] & MVM_CALLSITE_ARG_STR || flag_map[flag] & MVM_CALLSITE_ARG_OBJ)
                MVM_gc_worklist_add(tc, worklist, &ctx->args[i].o);
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCallCapture *ctx = (MVMCallCapture *)obj;
    if (ctx->body.mode == MVM_CALL_CAPTURE_MODE_SAVE) {
        /* We made our own copy of the args buffer and processing context, so
         * free them both. */
        if (ctx->body.apc) {
            MVM_checked_free_null(ctx->body.apc->args);
            MVM_checked_free_null(ctx->body.apc);
        }
    }
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
MVMREPROps * MVMCallCapture_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    &MVM_REPR_DEFAULT_ATTR_FUNCS,
    &MVM_REPR_DEFAULT_BOX_FUNCS,
    &MVM_REPR_DEFAULT_POS_FUNCS,
    &MVM_REPR_DEFAULT_ASS_FUNCS,
    NULL, /* elems */
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
    NULL, /* name */
    0, /* ID */
    1, /* refs_frames */
};