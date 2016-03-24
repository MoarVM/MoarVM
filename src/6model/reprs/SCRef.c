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
        st->size = sizeof(MVMSerializationContext);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMObject *root_codes, *rep_indexes, *rep_scs, *owned_objects, *rm;

    MVMInstance       *instance     = tc->instance;
    MVMObject         *BOOTIntArray = instance->boot_types.BOOTIntArray;
    MVMSerializationContextBody *sc = ((MVMSerializationContext *)root)->body;

    MVM_gc_root_temp_push(tc, (MVMCollectable **)&root);

    rep_indexes = REPR(BOOTIntArray)->allocate(tc, STABLE(BOOTIntArray));
    MVM_ASSIGN_REF(tc, &(root->header), sc->rep_indexes, rep_indexes);

    rm = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTReentrantMutex);
    MVM_ASSIGN_REF(tc, &(root->header), sc->mutex, rm);

    root_codes = REPR(instance->boot_types.BOOTArray)->allocate(tc, STABLE(instance->boot_types.BOOTArray));
    MVM_ASSIGN_REF(tc, &(root->header), sc->root_codes, root_codes);

    rep_scs = REPR(instance->boot_types.BOOTArray)->allocate(tc, STABLE(instance->boot_types.BOOTArray));
    MVM_ASSIGN_REF(tc, &(root->header), sc->rep_scs, rep_scs);

    owned_objects = REPR(instance->boot_types.BOOTArray)->allocate(tc, STABLE(instance->boot_types.BOOTArray));
    MVM_ASSIGN_REF(tc, &(root->header), sc->owned_objects, owned_objects);

    MVM_gc_root_temp_pop(tc);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation SCRef");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMSerializationContextBody *sc = ((MVMSerializationContextBody **)data)[0];
    MVMuint64 i;

    MVM_gc_worklist_add(tc, worklist, &sc->handle);
    MVM_gc_worklist_add(tc, worklist, &sc->description);
    MVM_gc_worklist_add(tc, worklist, &sc->root_codes);
    MVM_gc_worklist_add(tc, worklist, &sc->rep_indexes);
    MVM_gc_worklist_add(tc, worklist, &sc->rep_scs);
    MVM_gc_worklist_add(tc, worklist, &sc->owned_objects);

    for (i = 0; i < sc->num_objects; i++)
        MVM_gc_worklist_add(tc, worklist, &sc->root_objects[i]);
    for (i = 0; i < sc->num_stables; i++)
        MVM_gc_worklist_add(tc, worklist, &sc->root_stables[i]);

    MVM_gc_worklist_add(tc, worklist, &sc->sc);
    MVM_gc_worklist_add(tc, worklist, &sc->mutex);

    /* Mark serialization reader, if we have one. */
    if (sc->sr) {
        MVM_gc_worklist_add(tc, worklist, &(sc->sr->root.sc));
        for (i = 0; i < sc->sr->root.num_dependencies; i++)
            MVM_gc_worklist_add(tc, worklist, &(sc->sr->root.dependent_scs[i]));
        MVM_gc_worklist_add(tc, worklist, &(sc->sr->root.string_heap));
        MVM_gc_worklist_add(tc, worklist, &(sc->sr->root.string_comp_unit));
        MVM_gc_worklist_add(tc, worklist, &(sc->sr->codes_list));
        MVM_gc_worklist_add(tc, worklist, &(sc->sr->current_object));
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMSerializationContext *sc = (MVMSerializationContext *)obj;

    if (sc->body == NULL)
        return;

    /* Remove from weakref lookup hash (which doesn't count as a root). */
    uv_mutex_lock(&tc->instance->mutex_sc_weakhash);
    HASH_DELETE(hash_handle, tc->instance->sc_weakhash, sc->body);
    tc->instance->all_scs[sc->body->sc_idx] = NULL;
    uv_mutex_unlock(&tc->instance->mutex_sc_weakhash);

    /* Free manually managed object and STable root list memory. */
    MVM_free(sc->body->root_objects);
    MVM_free(sc->body->root_stables);

    /* If we have a serialization reader, clean that up too. */
    if (sc->body->sr) {
        if (sc->body->sr->data_needs_free)
            MVM_free(sc->body->sr->data);
        MVM_free(sc->body->sr->root.dependent_scs);
        MVM_free(sc->body->sr->contexts);
        MVM_free(sc->body->sr->wl_objects.indexes);
        MVM_free(sc->body->sr->wl_stables.indexes);
        MVM_free(sc->body->sr);
    }

    /* Free body. */
    MVM_free(sc->body);
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

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMSerializationContextBody     *body      = ((MVMSerializationContextBody **)data)[0];
    MVMuint64 size = 0;

    size += sizeof(MVMObject *) * body->num_objects;
    size += sizeof(MVMSTable *) * body->num_stables;

    /* XXX probably have to measure the MVMSerializationReader, too */

    return size;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMSerializationContextBody *body = ((MVMSerializationContextBody **)data)[0];
    MVMuint64 index;

    if (body->sr)
        return;

    for (index = 0; index < body->num_objects; index++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->root_objects[index], "Object root set");
    for (index = 0; index < body->num_stables; index++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->root_stables[index], "STable root set");

    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->root_codes, "Root code refs");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->rep_indexes, "Repossession indices");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->rep_scs, "Repossession SCs");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->owned_objects, "Owned Objects");

    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->handle, "Handle");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->description, "Description");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->sc, "SC");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->mutex, "Mutex");

    /* Mark serialization reader, if we have one. */
    if (body->sr) {
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->sr->root.sc, "Reader Root SC");
        for (index = 0; index < body->sr->root.num_dependencies; index++)
            MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                (MVMCollectable *)body->sr->root.dependent_scs[index], "SC Dependency (Reader)");
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->sr->root.string_heap, "String heap (Reader)");
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->sr->root.string_comp_unit, "String compilation unit (Reader)");
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->sr->codes_list, "Code objects list (Reader)");
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->sr->current_object, "Current object (Reader)");
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMSCRef_initialize(MVMThreadContext *tc) {
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
    NULL, /* spesh */
    "SCRef", /* name */
    MVM_REPR_ID_SCRef,
    0, /* refs_frames */
    unmanaged_size,
    describe_refs,
};
