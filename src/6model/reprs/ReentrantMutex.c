#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Populates the object body with a mutex. */
static void initialize_mutex(MVMThreadContext *tc, MVMReentrantMutexBody *rm) {
    int init_stat;
    rm->mutex = MVM_malloc(sizeof(uv_mutex_t));
    if ((init_stat = uv_mutex_init(rm->mutex)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMReentrantMutex);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    initialize_mutex(tc, (MVMReentrantMutexBody *)data);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation ReentrantMutex");
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    /* The ThreadContext has already been destroyed by the GC. */
    MVMReentrantMutex *rm = (MVMReentrantMutex *)obj;
    uv_mutex_destroy(rm->body.mutex);
    MVM_free(rm->body.mutex);
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

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMReentrantMutex);
}

/* Serializing a mutex doesn't save anything; we will re-create it upon
 * deserialization. Makes data structures that just happen to have a lock in
 * them serializable. */
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
}
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    initialize_mutex(tc, (MVMReentrantMutexBody *)data);
}

/* Initializes the representation. */
const MVMREPROps * MVMReentrantMutex_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
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
    serialize,
    deserialize,
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    NULL, /* gc_mark */
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "ReentrantMutex", /* name */
    MVM_REPR_ID_ReentrantMutex,
    0, /* refs_frames */
};

/* Locks the mutex. */
void MVM_reentrantmutex_lock(MVMThreadContext *tc, MVMReentrantMutex *rm) {
    if (MVM_load(&rm->body.holder_id) == tc->thread_id) {
        /* We already hold the lock; bump the count. */
        MVM_incr(&rm->body.lock_count);
    }
    else {
        /* Not holding the lock; obtain it. */
        MVMROOT(tc, rm, {
            MVM_gc_mark_thread_blocked(tc);
            uv_mutex_lock(rm->body.mutex);
            MVM_gc_mark_thread_unblocked(tc);
        });
        MVM_store(&rm->body.holder_id, tc->thread_id);
        MVM_store(&rm->body.lock_count, 1);
    }
}

/* Unlocks the mutex. */
void MVM_reentrantmutex_unlock(MVMThreadContext *tc, MVMReentrantMutex *rm) {
    /* Ensure we hold the lock. */
    if (MVM_load(&rm->body.holder_id) == tc->thread_id) {
        if (MVM_decr(&rm->body.lock_count) == 1) {
            /* Decremented the last recursion count; really unlock. */
            MVM_store(&rm->body.holder_id, 0);
            uv_mutex_unlock(rm->body.mutex);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Attempt to unlock mutex by thread not holding it");
    }
}
