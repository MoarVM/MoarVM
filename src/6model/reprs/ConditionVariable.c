#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps ConditionVariable_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &ConditionVariable_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMConditionVariable);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation ConditionVariable");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMConditionVariableBody *cv = (MVMConditionVariableBody *)data;
    MVM_gc_worklist_add(tc, worklist, &cv->mutex);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMConditionVariable *cv = (MVMConditionVariable *)obj;
    if (cv->body.condvar) {
        uv_cond_destroy(cv->body.condvar);
        MVM_free(cv->body.condvar);
        cv->body.condvar = NULL;
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
    st->size = sizeof(MVMConditionVariable);
}

/* Initializes the representation. */
const MVMREPROps * MVMConditionVariable_initialize(MVMThreadContext *tc) {
    return &ConditionVariable_this_repr;
}

static const MVMREPROps ConditionVariable_this_repr = {
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
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "ConditionVariable", /* name */
    MVM_REPR_ID_ConditionVariable,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Given a reentrant mutex, produces an associated condition variable. */
MVMObject * MVM_conditionvariable_from_lock(MVMThreadContext *tc, MVMReentrantMutex *lock, MVMObject *type) {
    MVMConditionVariable *cv;
    int init_stat;

    if (REPR(type)->ID != MVM_REPR_ID_ConditionVariable)
        MVM_exception_throw_adhoc(tc, "Condition variable must have ConditionVariable REPR");

    MVMROOT(tc, lock, {
        cv = (MVMConditionVariable *)MVM_gc_allocate_object(tc, STABLE(type));
    });
    cv->body.condvar = MVM_malloc(sizeof(uv_cond_t));
    if ((init_stat = uv_cond_init(cv->body.condvar)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize condition variable: %s",
            uv_strerror(init_stat));
    MVM_ASSIGN_REF(tc, &(cv->common.header), cv->body.mutex, (MVMObject *)lock);

    return (MVMObject *)cv;
}

/* Adds the current thread to the queue of waiters on the condition variable,
 * releasing, waiting, and then re-acquiring the lock. */
void MVM_conditionvariable_wait(MVMThreadContext *tc, MVMConditionVariable *cv) {
    MVMReentrantMutex *rm = (MVMReentrantMutex *)cv->body.mutex;
    AO_t orig_rec_level;
    unsigned int interval_id;

    if (MVM_load(&rm->body.holder_id) != tc->thread_id)
        MVM_exception_throw_adhoc(tc,
            "Can only wait on a condition variable when holding mutex");

    interval_id = startInterval(tc, "ConditionVariable.wait");
    annotateInterval(cv->body.condvar, interval_id, "this condition variable");
    orig_rec_level = MVM_load(&rm->body.lock_count);
    MVM_store(&rm->body.holder_id, 0);
    MVM_store(&rm->body.lock_count, 0);

    MVMROOT(tc, cv, {
    MVMROOT(tc, rm, {
        MVM_gc_mark_thread_blocked(tc);
        uv_cond_wait(cv->body.condvar, rm->body.mutex);
        MVM_gc_mark_thread_unblocked(tc);
    });
    });

    MVM_store(&rm->body.holder_id, tc->thread_id);
    MVM_store(&rm->body.lock_count, orig_rec_level);
    stopInterval(tc, interval_id, "ConditionVariable.wait");
}

/* Signals one thread waiting on the condition. */
void MVM_conditionvariable_signal_one(MVMThreadContext *tc, MVMConditionVariable *cv) {
    takeTimeStamp(tc, "ConditionVariable.signal_one");
    uv_cond_signal(cv->body.condvar);
}

/* Signals all threads waiting on the condition. */
void MVM_conditionvariable_signal_all(MVMThreadContext *tc, MVMConditionVariable *cv) {
    takeTimeStamp(tc, "ConditionVariable.signal_all");
    uv_cond_broadcast(cv->body.condvar);
}
