#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps Semaphore_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &Semaphore_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMSemaphore);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation Semaphore");
}

/* Set up the Semaphore with its initial value. */
static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
    int r;
    body->sem = MVM_malloc(sizeof(uv_sem_t));
    if ((r = uv_sem_init(body->sem, (MVMuint32) value)) < 0) {
        MVM_free(body->sem);
        body->sem = NULL;
        MVM_exception_throw_adhoc(tc, "Failed to initialize Semaphore: %s",
            uv_strerror(r));
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSemaphore *sem = (MVMSemaphore *)obj;
    if (sem->body.sem) {
        uv_sem_destroy(sem->body.sem);
        MVM_free(sem->body.sem);
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

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMSemaphore);
}

/* Initializes the representation. */
const MVMREPROps * MVMSemaphore_initialize(MVMThreadContext *tc) {
    return &Semaphore_this_repr;
}

static const MVMREPROps Semaphore_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        MVM_REPR_DEFAULT_GET_INT,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        MVM_REPR_DEFAULT_SET_UINT,
        MVM_REPR_DEFAULT_GET_UINT,
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
    deserialize_stable_size,
    NULL, /* gc_mark */
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "Semaphore", /* name */
    MVM_REPR_ID_Semaphore,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

MVMint64 MVM_semaphore_tryacquire(MVMThreadContext *tc, MVMSemaphore *sem) {
    int r;
    takeTimeStamp(tc, "Semaphore.tryAcquire");
    r = uv_sem_trywait(sem->body.sem);
    return !r;
}

void MVM_semaphore_acquire(MVMThreadContext *tc, MVMSemaphore *sem) {
    unsigned int interval_id;
    interval_id = startInterval(tc, "Semaphore.acquire");
    MVMROOT(tc, sem, {
        MVM_gc_mark_thread_blocked(tc);
        uv_sem_wait(sem->body.sem);
        MVM_gc_mark_thread_unblocked(tc);
    });
    stopInterval(tc, interval_id, "Semaphore.acquire");
}

void MVM_semaphore_release(MVMThreadContext *tc, MVMSemaphore *sem) {
    takeTimeStamp(tc, "Semaphore.release");
    uv_sem_post(sem->body.sem);
}
