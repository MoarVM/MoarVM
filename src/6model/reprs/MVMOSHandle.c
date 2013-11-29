#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMOSHandle);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    /* can't be copied because then we could never know when gc_free should
     * close the handle (unless we did some refcounting on a shared container).
     * note - 12:25 <jnthn> I mean, Perl 6 will has an attribute which
     * is the MoarVM handle, so a .clone() on a Perl 6 IO object
     * won't trigger cloning of the underlying handle.            */
    MVM_exception_throw_adhoc(tc, "Cannot copy object with repr OSHandle");
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMOSHandle *handle = (MVMOSHandle *)obj;
    switch(handle->body.type) {
        case MVM_OSHANDLE_UNINIT:
            break;
        case MVM_OSHANDLE_HANDLE:
            if (handle->body.u.handle
            && !uv_is_closing(handle->body.u.handle)
            && tc->instance->stdin_handle  != obj
            && tc->instance->stdout_handle != obj
            && tc->instance->stderr_handle != obj) {
                uv_close(handle->body.u.handle, NULL);
            }
            break;
        case MVM_OSHANDLE_FD:
            break;
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
const MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
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
    NULL, /* gc_mark */
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "MVMOSHandle", /* name */
    MVM_REPR_ID_MVMOSHandle,
    0, /* refs_frames */
};