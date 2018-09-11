#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps ConcBlockingQueue_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &ConcBlockingQueue_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMConcBlockingQueue);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMConcBlockingQueue *cbq = (MVMConcBlockingQueue*)root;
    MVMConcBlockingQueueBody *body = MVM_calloc(1, sizeof(MVMConcBlockingQueueBody));
    /* Initialize locks. */
    int init_stat;

    if ((init_stat = uv_mutex_init(&body->head_lock)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    if ((init_stat = uv_mutex_init(&body->tail_lock)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize mutex: %s",
            uv_strerror(init_stat));
    if ((init_stat = uv_cond_init(&body->head_cond)) < 0)
        MVM_exception_throw_adhoc(tc, "Failed to initialize condition variable: %s",
            uv_strerror(init_stat));

    /* Head and tail point to a null node. */
    body->tail = body->head = MVM_calloc(1, sizeof(MVMConcBlockingQueueNode));
    cbq->body = body;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation ConcBlockingQueue");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    /* At this point we know the world is stopped, and thus we can safely do a
     * traversal of the data structure without needing locks. */
    MVMConcBlockingQueueBody *cbq = *(MVMConcBlockingQueueBody **)data;
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
    MVMConcBlockingQueueBody *body = cbq->body;
    MVMConcBlockingQueueNode *cur = body->head;
    while (cur) {
        MVMConcBlockingQueueNode *next = cur->next;
        MVM_free(cur);
        cur = next;
    }
    body->head = body->tail = NULL;

    /* Clean up  */
    uv_mutex_destroy(&body->head_lock);
    uv_mutex_destroy(&body->tail_lock);
    uv_cond_destroy(&body->head_cond);

    /* Clean up body */
    MVM_free(body);
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
    MVMConcBlockingQueueBody *body = *(MVMConcBlockingQueueBody **)data;

    if (index != 0)
        MVM_exception_throw_adhoc(tc,
            "Can only request (peek) head of a concurrent blocking queue");
    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc,
            "Can only get objects from a concurrent blocking queue");

    if (MVM_load(&body->elems) > 0) {
        MVMConcBlockingQueueNode *peeked;
        unsigned int interval_id;
        interval_id = MVM_telemetry_interval_start(tc, "ConcBlockingQueue.at_pos");
        MVMROOT(tc, root, {
            MVM_gc_mark_thread_blocked(tc);
            uv_mutex_lock(&body->head_lock);
            MVM_gc_mark_thread_unblocked(tc);
        });
        peeked = body->head->next;
        value->o = peeked ? peeked->value : tc->instance->VMNull;
        uv_mutex_unlock(&body->head_lock);
        MVM_telemetry_interval_stop(tc, interval_id, "ConcBlockingQueue.at_pos");
    }
    else {
        value->o = tc->instance->VMNull;
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMConcBlockingQueueBody *cbq = *(MVMConcBlockingQueueBody **)data;
    return MVM_load(&(cbq->elems));
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *body = *(MVMConcBlockingQueueBody**)data;
    MVMConcBlockingQueueNode *add;
    AO_t orig_elems;
    MVMObject *to_add = value.o;
    unsigned int interval_id;

    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc,
            "Can only push objects to a concurrent blocking queue");
    if (value.o == NULL)
        MVM_exception_throw_adhoc(tc,
            "Cannot store a null value in a concurrent blocking queue");

    add = MVM_calloc(1, sizeof(MVMConcBlockingQueueNode));

    interval_id = MVM_telemetry_interval_start(tc, "ConcBlockingQueue.push");
    MVMROOT2(tc, root, to_add, {
        MVM_gc_mark_thread_blocked(tc);
        uv_mutex_lock(&body->tail_lock);
        MVM_gc_mark_thread_unblocked(tc);
    });
    MVM_ASSIGN_REF(tc, &(root->header), add->value, to_add);
    body->tail->next = add;
    body->tail = add;
    orig_elems = MVM_incr(&body->elems);
    uv_mutex_unlock(&body->tail_lock);

    if (orig_elems == 0) {
        MVMROOT(tc, root, {
            MVM_gc_mark_thread_blocked(tc);
            uv_mutex_lock(&body->head_lock);
            MVM_gc_mark_thread_unblocked(tc);
        });
        uv_cond_signal(&body->head_cond);
        uv_mutex_unlock(&body->head_lock);
    }
    MVM_telemetry_interval_stop(tc, interval_id, "ConcBlockingQueue.push");
}

/* Push to front of the queue - this should be an exceptional case, it has less
 * concurrency than the pair of push/shift */
static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *cbq = *(MVMConcBlockingQueueBody **)data;
    MVMConcBlockingQueueNode *add;
    MVMObject *to_add = value.o;
    unsigned int interval_id;
    AO_t orig_elems;

    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc,
            "Can only push objects to a concurrent blocking queue");
    if (value.o == NULL)
        MVM_exception_throw_adhoc(tc,
            "Cannot store a null value in a concurrent blocking queue");

    interval_id = MVM_telemetry_interval_start(tc, "ConcBlockingQueue.unshift");

    add = MVM_calloc(1, sizeof(MVMConcBlockingQueueNode));
    MVM_ASSIGN_REF(tc, &(root->header), add->value, to_add);

    /* We'll need to hold both the head and the tail lock, in case head == tail
     * and push would update tail->next - without the tail lock, this could
     * race. Ensure that we lock in the same order */
    MVMROOT2(tc, root, to_add, {
        MVM_gc_mark_thread_blocked(tc);
        uv_mutex_lock(&cbq->tail_lock);
        uv_mutex_lock(&cbq->head_lock);
        MVM_gc_mark_thread_unblocked(tc);
    });

    add->next = cbq->head->next;
    cbq->head->next = add;

    if (MVM_incr(&cbq->elems) == 0) {
        /* add to tail as well - we still have that lock, so we can't race */
        cbq->tail = add;
        /* signal sleeping threads */
        uv_cond_signal(&cbq->head_cond);
    }

    uv_mutex_unlock(&cbq->head_lock);
    uv_mutex_unlock(&cbq->tail_lock);

    MVM_telemetry_interval_stop(tc, interval_id, "ConcBlockingQueue.unshift");
}


static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMConcBlockingQueueBody *body = *(MVMConcBlockingQueueBody**)data;
    MVMConcBlockingQueueNode *taken;
    unsigned int interval_id;

    if (kind != MVM_reg_obj)
        MVM_exception_throw_adhoc(tc, "Can only shift objects from a ConcBlockingQueue");

    interval_id = MVM_telemetry_interval_start(tc, "ConcBlockingQueue.shift");
    MVMROOT(tc, root, {
        MVM_gc_mark_thread_blocked(tc);
        uv_mutex_lock(&body->head_lock);
        MVM_gc_mark_thread_unblocked(tc);

        while (MVM_load(&body->elems) == 0) {
                MVM_gc_mark_thread_blocked(tc);
                uv_cond_wait(&body->head_cond, &body->head_lock);
                MVM_gc_mark_thread_unblocked(tc);
        }
    });

    taken = body->head->next;
    MVM_free(body->head);
    body->head = taken;
    MVM_barrier();
    value->o = taken->value;
    taken->value = NULL;
    MVM_barrier();

    if (MVM_decr(&body->elems) > 1)
        uv_cond_signal(&body->head_cond);

    uv_mutex_unlock(&body->head_lock);
    MVM_telemetry_interval_stop(tc, interval_id, "ConcBlockingQueue.shift");
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMConcBlockingQueue);
}

/* Initializes the representation. */
const MVMREPROps * MVMConcBlockingQueue_initialize(MVMThreadContext *tc) {
    return &ConcBlockingQueue_this_repr;
}

static const MVMREPROps ConcBlockingQueue_this_repr = {
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
        unshift,
        shift,
        MVM_REPR_DEFAULT_SLICE,
        MVM_REPR_DEFAULT_SPLICE,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS,
        MVM_REPR_DEFAULT_SET_DIMENSIONS,
        MVM_REPR_DEFAULT_GET_ELEM_STORAGE_SPEC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC_MULTIDIM
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
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

MVMObject * MVM_concblockingqueue_jit_poll(MVMThreadContext *tc, MVMObject *queue) {
    if (REPR(queue)->ID == MVM_REPR_ID_ConcBlockingQueue && IS_CONCRETE(queue))
        return MVM_concblockingqueue_poll(tc, (MVMConcBlockingQueue *)queue);
    else
        MVM_exception_throw_adhoc(tc,
                "queuepoll requires a concrete object with REPR ConcBlockingQueue");
}

/* Polls a queue for a value, returning NULL if none is available. */
MVMObject * MVM_concblockingqueue_poll(MVMThreadContext *tc, MVMConcBlockingQueue *queue) {
    MVMConcBlockingQueue *cbq = (MVMConcBlockingQueue *)queue;
    MVMConcBlockingQueueBody *body = cbq->body;
     MVMConcBlockingQueueNode *taken;
    MVMObject *result = tc->instance->VMNull;
    unsigned int interval_id;

    interval_id = MVM_telemetry_interval_start(tc, "ConcBlockingQueue.poll");
    MVMROOT(tc, cbq, {
        MVM_gc_mark_thread_blocked(tc);
        uv_mutex_lock(&body->head_lock);
        MVM_gc_mark_thread_unblocked(tc);
    });

    if (MVM_load(&body->elems) > 0) {
        taken = body->head->next;
        MVM_free(body->head);
        body->head = taken;
        MVM_barrier();
        result = taken->value;
        taken->value = NULL;
        MVM_barrier();
        if (MVM_decr(&body->elems) > 1)
            uv_cond_signal(&body->head_cond);
    }

    uv_mutex_unlock(&body->head_lock);

    MVM_telemetry_interval_stop(tc, interval_id, "ConcBlockingQueue.poll");
    return result;
}
