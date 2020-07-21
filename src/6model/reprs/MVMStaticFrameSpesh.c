#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps StaticFrameSpesh_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &StaticFrameSpesh_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMStaticFrameSpesh);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation StaticFrameSpesh");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStaticFrameSpeshBody *body = (MVMStaticFrameSpeshBody *)data;
    MVM_spesh_stats_gc_mark(tc, body->spesh_stats, worklist);
    MVM_spesh_arg_guard_gc_mark(tc, body->spesh_arg_guard, worklist);
    if (body->num_spesh_candidates) {
        MVMuint32 i;
        for (i = 0; i < body->num_spesh_candidates; i++) {
            MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i]);
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMStaticFrameSpesh *sfs = (MVMStaticFrameSpesh *)obj;
    MVM_spesh_stats_destroy(tc, sfs->body.spesh_stats);
    MVM_free(sfs->body.spesh_stats);
    MVM_spesh_arg_guard_destroy(tc, sfs->body.spesh_arg_guard, 0);
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
    st->size = sizeof(MVMStaticFrameSpesh);
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMStaticFrameSpeshBody *body = (MVMStaticFrameSpeshBody *)data;

    MVM_spesh_stats_gc_describe(tc, ss, body->spesh_stats);
}

/* Initializes the representation. */
const MVMREPROps * MVMStaticFrameSpesh_initialize(MVMThreadContext *tc) {
    return &StaticFrameSpesh_this_repr;
}

static const MVMREPROps StaticFrameSpesh_this_repr = {
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
    "MVMStaticFrameSpesh", /* name */
    MVM_REPR_ID_MVMStaticFrameSpesh,
    NULL, /* unmanaged_size */
    describe_refs
};
