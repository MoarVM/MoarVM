#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMSerializationContext);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMSerializationContextBody *sc = ((MVMSerializationContext *)root)->body;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *root_objects, *root_codes;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&BOOTArray);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&root);

    root_objects = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_ASSIGN_REF(tc, root, sc->root_objects, root_objects);
    REPR(root_objects)->initialize(tc, STABLE(root_objects), root_objects, OBJECT_BODY(root_objects));

    root_codes = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_ASSIGN_REF(tc, root, sc->root_codes, root_codes);
    REPR(root_codes)->initialize(tc, STABLE(root_codes), root_codes, OBJECT_BODY(root_codes));

    MVM_gc_root_temp_pop_n(tc, 2);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation SCRef");
}

void MVM_sc_gc_mark_body(MVMThreadContext *tc, MVMSerializationContextBody *sc, MVMGCWorklist *worklist) {
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMSerializationContextBody *sc = ((MVMSerializationContextBody **)data)[0];
    MVMuint64 i;

    MVM_gc_worklist_add(tc, worklist, &sc->handle);
    MVM_gc_worklist_add(tc, worklist, &sc->description);
    MVM_gc_worklist_add(tc, worklist, &sc->root_objects);
    MVM_gc_worklist_add(tc, worklist, &sc->root_codes);

    for (i = 0; i < sc->num_stables; i++)
        MVM_gc_worklist_add(tc, worklist, &sc->root_stables[i]);

    MVM_gc_worklist_add(tc, worklist, &sc->sc);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSerializationContext *sc = (MVMSerializationContext *)obj;

    /* Remove from weakref lookup hash (which doesn't count as a root). */
    uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
    HASH_DELETE(hash_handle, tc->instance->sc_weakhash, sc->body);
    uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);

    /* Free manually managed STable list memory and body. */
    MVM_checked_free_null(sc->body->root_stables);
    MVM_checked_free_null(sc->body);
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
MVMREPROps * MVMSCRef_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    &MVM_REPR_DEFAULT_ATTR_FUNCS,
    &MVM_REPR_DEFAULT_BOX_FUNCS,
    &MVM_REPR_DEFAULT_POS_FUNCS,
    &MVM_REPR_DEFAULT_ASS_FUNCS,
    NULL, /* elems */
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
    "SCRef", /* name */
    MVM_REPR_ID_SCRef,
    0, /* refs_frames */
};