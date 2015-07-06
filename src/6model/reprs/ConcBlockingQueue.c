#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMConcBlockingQueue);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;

    /* Initialize locks. */
    int init_stat;
    cbq->locks = MVM_calloc(1, sizeof(MVMConcBlockingQueueLocks));
    if ((init_stat = uv_mutex_init(&cbq->locks->head_lock)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    if ((init_stat = uv_mutex_init(&cbq->locks->tail_lock)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    if ((init_stat = uv_cond_init(&cbq->locks->head_cond)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize condition variable: %s",
            uv_strerror(init_stat));

    /* Head and tail point to a null node. */
    cbq->tail = cbq->head = MVM_calloc(1, sizeof(MVMConcBlockingQueueNode));
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation ConcBlockingQueue");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    /* At this point we know the world is stopped, and thus we can safely do a
     * traversal of the data structure without needing locks. */
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;
    MVMConcBlockingQueueNode *cur = cbq->head;
    while (cur) {
        MVM_gc_worklist_add(tc, worklist, &cur->value);
        cur = cur->next;
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMConcBlockingQueue *cbq = (MVMConcBlockingQueue *)obj;

    /* First, free all the nodes. */
    MVMConcBlockingQueueNode *cur = cbq->body.head;
    while (cur) {
        MVMConcBlockingQueueNode *next = cur->next;
        MVM_free(cur);
        cur = next;
    }
    cbq->body.head = cbq->body.tail = NULL;

    /* Clean up locks. */
    uv_mutex_destroy(&cbq->body.locks->head_lock);
    uv_mutex_destroy(&cbq->body.locks->tail_lock);
    uv_cond_destroy(&cbq->body.locks->head_cond);
    MVM_free(cbq->body.locks);
    cbq->body.locks = NULL;
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

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;

    if (index != 0)
        MVM_exception_throw_adhoc(tc,
            "Can only request (peek) head of a concurrent blocking queue");
    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc,
            "Can only get objects from a concurrent blocking queue");

    if (MVM_load(&cbq->elems) > 0) {
        MVMConcBlockingQueueNode *peeked;
        uv_mutex_lock(&cbq->locks->head_lock);
        peeked = cbq->head->next;
        value->o = peeked ? peeked->value : tc->instance->VMNull;
        uv_mutex_unlock(&cbq->locks->head_lock);
    }
    else {
        value->o = tc->instance->VMNull;
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;
    return MVM_load(cbq->elems);
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;
    MVMConcBlockingQueueNode *add;
    AO_t orig_elems;

    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc,
            "Can only push objects to a concurrent blocking queue");
    if (value.o == NULL)
        MVM_exception_throw_adhoc(tc,
            "Cannot store a null value in a concurrent blocking queue");

    add = MVM_calloc(1, sizeof(MVMConcBlockingQueueNode));
    MVM_ASSIGN_REF(tc, &(root->header), add->value, value.o);

    uv_mutex_lock(&cbq->locks->tail_lock);
    cbq->tail->next = add;
    cbq->tail = add;
    orig_elems = MVM_incr(&cbq->elems);
    uv_mutex_unlock(&cbq->locks->tail_lock);

    if (orig_elems == 0) {
        uv_mutex_lock(&cbq->locks->head_lock);
        uv_cond_signal(&cbq->locks->head_cond);
        uv_mutex_unlock(&cbq->locks->head_lock);
    }
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *cbq = (MVMConcBlockingQueueBody *)data;
    MVMConcBlockingQueueNode *taken;

    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc, "Can only shift objects from a ConcBlockingQueue");

    uv_mutex_lock(&cbq->locks->head_lock);

    while (MVM_load(&cbq->elems) == 0) {
        MVMROOT(tc, root, {
            MVM_gc_mark_thread_blocked(tc);
            uv_cond_wait(&cbq->locks->head_cond, &cbq->locks->head_lock);
            MVM_gc_mark_thread_unblocked(tc);
            data = OBJECT_BODY(root);
            cbq  = (MVMConcBlockingQueueBody *)data;
        });
    }

    taken = cbq->head->next;
    MVM_free(cbq->head);
    cbq->head = taken;
    MVM_barrier();
    value->o = taken->value;
    taken->value = NULL;
    MVM_barrier();

    if (MVM_decr(&cbq->elems) > 1)
        uv_cond_signal(&cbq->locks->head_cond);

    uv_mutex_unlock(&cbq->locks->head_lock);
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMConcBlockingQueue);
}

/* Initializes the representation. */
const MVMREPROps * MVMConcBlockingQueue_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    initialize,
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    {
        at_pos,
        MVM_REPR_DEFAULT_BIND_POS,
        MVM_REPR_DEFAULT_SET_ELEMS,
        push,
        MVM_REPR_DEFAULT_POP,
        MVM_REPR_DEFAULT_UNSHIFT,
        shift,
        MVM_REPR_DEFAULT_SPLICE,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS,
        MVM_REPR_DEFAULT_SET_DIMENSIONS,
        MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC
    },    /* pos_funcs */
    MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
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
    "ConcBlockingQueue", /* name */
    MVM_REPR_ID_ConcBlockingQueue,
    0, /* refs_frames */
};

/* Polls a queue for a value, returning NULL if none is available. */
MVMObject * MVM_concblockingqueue_poll(MVMThreadContext *tc, MVMConcBlockingQueue *queue) {
    MVMConcBlockingQueue *cbq = (MVMConcBlockingQueue *)queue;
    MVMConcBlockingQueueNode *taken;
    MVMObject *result = tc->instance->VMNull;

    uv_mutex_lock(&cbq->body.locks->head_lock);

    if (MVM_load(&cbq->body.elems) > 0) {
        taken = cbq->body.head->next;
        MVM_free(cbq->body.head);
        cbq->body.head = taken;
        MVM_barrier();
        result = taken->value;
        taken->value = NULL;
        MVM_barrier();
        if (MVM_decr(&cbq->body.elems) > 1)
            uv_cond_signal(&cbq->body.locks->head_cond);
    }

    uv_mutex_unlock(&cbq->body.locks->head_lock);

    return result;
}
