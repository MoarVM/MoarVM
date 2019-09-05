#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMOSHandle_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &MVMOSHandle_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMOSHandle);
    });

    return st->WHAT;
}

/* Initializes the handle with the mutex all handles need. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMOSHandleBody *handle = (MVMOSHandleBody *)data;
    handle->mutex = MVM_malloc(sizeof(uv_mutex_t));
    uv_mutex_init(handle->mutex);
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

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMOSHandleBody *handle = (MVMOSHandleBody *)data;
    if (handle->ops && handle->ops->gc_mark)
        handle->ops->gc_mark(tc, handle->data, worklist);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMOSHandle *handle = (MVMOSHandle *)obj;
    if (handle->body.ops && handle->body.ops->gc_free) {
        handle->body.ops->gc_free(tc, obj, handle->body.data);
        handle->body.data = NULL;
    }
    if (handle->body.mutex) {
        uv_mutex_destroy(handle->body.mutex);
        MVM_free(handle->body.mutex);
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
const MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc) {
    return &MVMOSHandle_this_repr;
}

static const MVMREPROps MVMOSHandle_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
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
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    NULL, /* jit */
    "MVMOSHandle", /* name */
    MVM_REPR_ID_MVMOSHandle,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
