#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps SpeshLog_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &SpeshLog_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMSpeshLog);
    });

    return st->WHAT;
}

/* Initializes the log. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMSpeshLogBody *log = (MVMSpeshLogBody *)data;
    log->entries = MVM_malloc(sizeof(MVMSpeshLogEntry) * MVM_SPESH_LOG_DEFAULT_ENTRIES);
    log->limit = MVM_SPESH_LOG_DEFAULT_ENTRIES;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation SpeshLog");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMSpeshLogBody *log = (MVMSpeshLogBody *)data;
    MVMuint32 i;
    MVM_gc_worklist_add(tc, worklist, &(log->thread));
    if (!log->entries)
        return;
    for (i = 0; i < log->used; i++) {
        switch (log->entries[i].kind) {
            case MVM_SPESH_LOG_ENTRY:
                MVM_gc_worklist_add(tc, worklist, &(log->entries[i].entry.sf));
                break;
            case MVM_SPESH_LOG_PARAMETER:
            case MVM_SPESH_LOG_PARAMETER_DECONT:
                MVM_gc_worklist_add(tc, worklist, &(log->entries[i].param.type));
                break;
            case MVM_SPESH_LOG_TYPE:
            case MVM_SPESH_LOG_RETURN:
                MVM_gc_worklist_add(tc, worklist, &(log->entries[i].type.type));
                break;
            case MVM_SPESH_LOG_INVOKE:
                MVM_gc_worklist_add(tc, worklist, &(log->entries[i].invoke.sf));
                break;
        }
    }
}

static void describe_refs (MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMSpeshLogBody  *body      = (MVMSpeshLogBody *)data;
    MVMuint64         i         = 0;

    MVMuint64 cache_1 = 0;
    MVMuint64 cache_2 = 0;
    MVMuint64 cache_3 = 0;
    MVMuint64 cache_4 = 0;
    MVMuint64 cache_5 = 0;
    MVMuint64 cache_6 = 0;
    MVMuint64 cache_7 = 0;

    if (!body->entries)
        return;
    for (i = 0; i < body->used; i++) {
        switch (body->entries[i].kind) {
            case MVM_SPESH_LOG_ENTRY:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].entry.sf, "Spesh log entry", &cache_1);
                break;
            case MVM_SPESH_LOG_PARAMETER:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].param.type, "Parameter entry", &cache_2);
                break;
            case MVM_SPESH_LOG_PARAMETER_DECONT:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].param.type, "Deconted parameter entry", &cache_3);
                break;
            case MVM_SPESH_LOG_TYPE:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].type.type, "Type entry", &cache_4);
                break;
            case MVM_SPESH_LOG_RETURN:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].type.type, "Return entry", &cache_5);
                break;
            case MVM_SPESH_LOG_INVOKE:
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->entries[i].invoke.sf, "Invoked staticframe entry", &cache_7);
                break;
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSpeshLog *log = (MVMSpeshLog *)obj;
    MVM_free(log->body.entries);
    if (log->body.block_condvar) {
        uv_cond_destroy(log->body.block_condvar);
        MVM_free(log->body.block_condvar);
    }
    if (log->body.block_mutex) {
        uv_mutex_destroy(log->body.block_mutex);
        MVM_free(log->body.block_mutex);
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
    st->size = sizeof(MVMSpeshLog);
}

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMSpeshLogBody *log = (MVMSpeshLogBody *)data;
    return log->limit * sizeof(MVMSpeshLogEntry);
}

/* Initializes the representation. */
const MVMREPROps * MVMSpeshLog_initialize(MVMThreadContext *tc) {
    return &SpeshLog_this_repr;
}

static const MVMREPROps SpeshLog_this_repr = {
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
    deserialize_stable_size,
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMSpeshLog", /* name */
    MVM_REPR_ID_MVMSpeshLog,
    unmanaged_size,
    describe_refs
};
