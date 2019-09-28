#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps SpeshPluginState_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &SpeshPluginState_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMSpeshPluginState);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation SpeshPluginState");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMSpeshPluginStateBody *body = (MVMSpeshPluginStateBody *)data;
    MVMuint32 i;
    for (i = 0; i < body->num_positions; i++) {
        MVMSpeshPluginGuardSet *gs = body->positions[i].guard_set;
        MVM_spesh_plugin_guard_list_mark(tc, gs->guards, gs->num_guards, worklist);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSpeshPluginState *sps = (MVMSpeshPluginState *)obj;
    MVMuint32 i;
    for (i = 0; i < sps->body.num_positions; i++) {
        MVM_fixed_size_free(tc, tc->instance->fsa,
                sps->body.positions[i].guard_set->num_guards * sizeof(MVMSpeshPluginGuard),
                sps->body.positions[i].guard_set->guards);
        MVM_fixed_size_free(tc, tc->instance->fsa, sizeof(MVMSpeshPluginGuardSet),
                sps->body.positions[i].guard_set);
    }
    MVM_fixed_size_free(tc, tc->instance->fsa,
            sps->body.num_positions * sizeof(MVMSpeshPluginPosition),
            sps->body.positions);
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
    st->size = sizeof(MVMSpeshPluginState);
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMSpeshPluginStateBody *body = (MVMSpeshPluginStateBody *)data;
    MVMuint64 size = 0;
    return size;
}

/* Initializes the representation. */
const MVMREPROps * MVMSpeshPluginState_initialize(MVMThreadContext *tc) {
    return &SpeshPluginState_this_repr;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMSpeshPluginStateBody *body = (MVMSpeshPluginStateBody *)data;
}

static const MVMREPROps SpeshPluginState_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL,
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
    "MVMSpeshPluginState", /* name */
    MVM_REPR_ID_MVMSpeshPluginState,
    unmanaged_size,
    describe_refs
};
