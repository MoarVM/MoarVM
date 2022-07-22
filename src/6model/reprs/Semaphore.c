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

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
#ifdef MVM_USE_C11_ATOMICS
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
    atomic_init(&body->count, 0);
    atomic_thread_fence(memory_order_acquire);
    atomic_flag_clear_explicit(&body->count, memory_order_release);
#endif
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)dest;
#ifdef MVM_USE_C11_ATOMICS
    atomic_thread_fence(memory_order_acquire);
    atomic_store_explicit(&body->count,
        atomic_load_explicit(&((MVMSemaphore *)dest_root)->body.count, memory_order_relaxed),
        memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
#else
    MVM_barrier();
    MVM_store(&body->count, MVM_load(&((MVMSemaphore *)dest_root)->body.count));
    MVM_barrier();
#endif
}

/* Set up the Semaphore with its initial value. */
static void set_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
#ifdef MVM_USE_C11_ATOMICS
    atomic_thread_fence(memory_order_acquire);
    atomic_store_explicit(&body->count, value, memory_order_release);
#else
    MVM_barrier();
    MVM_store(&body->count, value);
#endif
}

static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
    MVMuint64 count;
#ifdef MVM_USE_C11_ATOMICS
    count = atomic_load_explicit(&body->count, memory_order_acquire);
    atomic_thread_fence(memory_order_release);
#else
    count = MVM_load(&body->count);
    MVM_barrier();
#endif
    return count;
}
/* Set up the Semaphore with its initial value. */
static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
#ifdef MVM_USE_C11_ATOMICS
    atomic_thread_fence(memory_order_acquire);
    atomic_store_explicit(&body->count, (MVMuint64)value, memory_order_release);
#else
    MVM_barrier();
    MVM_store(&body->count, (MVMuint64)value);
#endif
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMSemaphoreBody *body = (MVMSemaphoreBody *)data;
    MVMint64 count;
#ifdef MVM_USE_C11_ATOMICS
    count = (MVMint64)atomic_load_explicit(&body->count, memory_order_acquire);
    atomic_thread_fence(memory_order_release);
#else
    count = (MVMint64)MVM_load(&body->count);
    MVM_barrier();
#endif
    return count;
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
    initialize,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        set_uint,
        get_uint,
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
    NULL, /* gc_free */
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

MVMuint64 MVM_semaphore_tryacquire(MVMThreadContext *tc, MVMSemaphore *sem) {
    MVMuint64 count, minus;
    MVM_telemetry_timestamp(tc, "Semaphore.try_acquire");
#ifdef MVM_USE_C11_ATOMICS
    count = atomic_load_explicit(&sem->body.count, memory_order_acquire);
    if ((minus = count))
        atomic_store_explicit(&sem->body.count, minus -= 1, memory_order_release);
#else
    MVM_barrier();
    count = MVM_load(&sem->body.count);
    MVM_store(count, minus = count && count - 1);
    MVM_barrier();
#endif
    return minus < count;
}

void MVM_semaphore_acquire(MVMThreadContext *tc, MVMSemaphore *sem) {
    unsigned int interval_id = MVM_telemetry_interval_start(tc, "Semaphore.acquire");
    MVMROOT(tc, sem, {
        MVMuint64 count;
        MVM_gc_mark_thread_blocked(tc);
#ifdef MVM_USE_C11_ATOMICS
        while (atomic_flag_test_and_set_explicit(&sem->body.waits, memory_order_acquire))
            atomic_thread_fence(memory_order_release), MVM_thread_yield(tc);
        while (!(count = atomic_load_explicit(&sem->body.count, memory_order_acquire)))
            atomic_thread_fence(memory_order_release), MVM_thread_yield(tc);
        atomic_store_explicit(&sem->body.count, count - 1, memory_order_release);
        atomic_flag_clear_explicit(&sem->body.waits, memory_order_release);
#else
        while (MVM_cas(&sem->body.waits, 0, 1))
            MVM_thread_yield(tc);
        while (MVM_barrier(), !(count = MVM_load(&sem->body.count)))
            MVM_thread_yield(tc);
        MVM_store(&sem->body.count, count - 1);
        MVM_store(&sem->body.waits, 0);
#endif
        MVM_gc_mark_thread_unblocked(tc);
    });
    MVM_telemetry_interval_stop(tc, interval_id, "Semaphore.acquire");
}

void MVM_semaphore_release(MVMThreadContext *tc, MVMSemaphore *sem) {
    MVMuint64 count;
    MVM_telemetry_timestamp(tc, "Semaphore.release");
#ifdef MVM_USE_C11_ATOMICS
    if ((count = atomic_load_explicit(&sem->body.count, memory_order_acquire)) == UINT64_MAX)
        MVM_exception_throw_adhoc(tc, "semaphore count must be no greater than %" PRIu64 "", UINT64_MAX);
    atomic_store_explicit(&sem->body.count, count + 1, memory_order_release);
#else
    MVM_barrier();
    if ((count = MVM_load(&sem->body.count)) == UINT64_MAX)
        MVM_exception_throw_adhoc(tc, "semaphore count must be no greater than %" PRIu64 "", UINT64_MAX);
    MVM_store(&sem->body.count, count + 1);
    MVM_barrier();
#endif
}
