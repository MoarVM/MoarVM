#include "moar.h"
#include "platform/mmap.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "Cannot invoke comp unit object");
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMCompUnit);
    });

    return st->WHAT;
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCompUnit *cu = (MVMCompUnit *)root;
    MVMROOT(tc, cu, {
        MVMObject *rm = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTReentrantMutex);
        MVM_ASSIGN_REF(tc, &(root->header), cu->body.deserialize_frame_mutex, rm);
        cu->body.inline_tweak_mutex = MVM_malloc(sizeof(uv_mutex_t));
        uv_mutex_init(cu->body.inline_tweak_mutex);
    });
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "this representation (CompUnit) cannot be cloned");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCompUnitBody *body = (MVMCompUnitBody *)data;
    MVMuint32 i;

    /* Add code refs to the worklists. */
    for (i = 0; i < body->num_frames; i++)
        MVM_gc_worklist_add(tc, worklist, &body->coderefs[i]);

    /* Add extop names to the worklist. */
    for (i = 0; i < body->num_extops; i++)
        MVM_gc_worklist_add(tc, worklist, &body->extops[i].name);

    /* Add strings to the worklists. */
    for (i = 0; i < body->num_strings; i++)
        MVM_gc_worklist_add(tc, worklist, &body->strings[i]);

    /* Add serialization contexts to the worklist. */
    for (i = 0; i < body->num_scs; i++) {
        if (body->scs[i]) {
            MVM_gc_worklist_add(tc, worklist, &body->scs[i]);
        }
        /* Unresolved sc bodies' handles are marked by the GC instance root marking. */
    }

    MVM_gc_worklist_add(tc, worklist, &body->deserialize_frame_mutex);

    /* Add various other referenced strings, etc. */
    MVM_gc_worklist_add(tc, worklist, &body->hll_name);
    MVM_gc_worklist_add(tc, worklist, &body->filename);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCompUnitBody *body = &((MVMCompUnit *)obj)->body;

    int i;
    for (i = 0; i < body->num_callsites; i++) {
        MVMCallsite *cs = body->callsites[i];
        if (!cs->is_interned)
            MVM_callsite_destroy(cs);
    }

    uv_mutex_destroy(body->inline_tweak_mutex);
    MVM_free(body->inline_tweak_mutex);
    MVM_free(body->coderefs);
    if (body->callsites)
        MVM_fixed_size_free(tc, tc->instance->fsa,
            body->num_callsites * sizeof(MVMCallsite *),
            body->callsites);
    if (body->extops)
        MVM_fixed_size_free(tc, tc->instance->fsa,
            body->num_extops * sizeof(MVMExtOpRecord),
            body->extops);
    if (body->strings)
        MVM_fixed_size_free(tc, tc->instance->fsa,
            body->num_strings * sizeof(MVMString *),
            body->strings);
    MVM_free(body->scs);
    MVM_free(body->scs_to_resolve);
    MVM_free(body->sc_handle_idxs);
    MVM_free(body->string_heap_fast_table);
    switch (body->deallocate) {
    case MVM_DEALLOCATE_NOOP:
        break;
    case MVM_DEALLOCATE_FREE:
        MVM_free(body->data_start);
        break;
    case MVM_DEALLOCATE_UNMAP:
        MVM_platform_unmap_file(body->data_start, body->handle, body->data_size);
        break;
    default:
        MVM_panic(MVM_exitcode_NYI, "Invalid deallocate of %u during MVMCompUnit gc_free", body->deallocate);
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
    /* XXX in the end we'll support inlining of this... */
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMCompUnitBody *body = (MVMCompUnitBody *)data;
    MVMuint64 size = 0;
    MVMuint32 index;

    size += sizeof(MVMCallsite *) * body->num_callsites;
    for (index = 0; index < body->num_callsites; index++) {
        MVMCallsite *cs = body->callsites[index];
        if (cs && !cs->is_interned) {
            size += sizeof(MVMCallsite);

            size += sizeof(MVMCallsiteEntry) * cs->flag_count;

            size += sizeof(MVMString *) * MVM_callsite_num_nameds(tc, cs);
        }
    }

    if (body->deallocate == MVM_DEALLOCATE_FREE) {
        /* XXX do we want to count mmapped data for the bytecode segment, too? */
        size += body->data_size;
    }

    size += sizeof(MVMObject *) * body->num_frames;

    size += sizeof(MVMExtOpRecord *) * body->num_extops;

    size += sizeof(MVMString *) * body->num_strings;

    size += body->serialized_size;

    /* since SCs are GC-managed themselves, only the array containing them
     * is added to the unmanaged size here. */
    size += body->num_scs * (
            sizeof(MVMSerializationContext *) +     /* scs */
            sizeof(MVMSerializationContextBody *) + /* scs_to_resolve */
            sizeof(MVMint32)                        /* sc_handle_idxs */
            );

    return size;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMCompUnitBody     *body      = (MVMCompUnitBody *)data;
    MVMuint32 i;

    /* Add code refs to the worklists. */
    for (i = 0; i < body->num_frames; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->coderefs[i], "Code refs array entry");

    /* Add extop names to the worklist. */
    for (i = 0; i < body->num_extops; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->extops[i].name, "Ext-op names list entry");

    /* Add strings to the worklists. */
    for (i = 0; i < body->num_strings; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->strings[i], "Strings heap entry");

    /* Add serialization contexts to the worklist. */
    for (i = 0; i < body->num_scs; i++)
        MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
            (MVMCollectable *)body->scs[i], "Serialization context dependency");

    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->deserialize_frame_mutex, "Update_mutex");

    /* Add various other referenced strings, etc. */
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->hll_name, "HLL name");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
        (MVMCollectable *)body->filename, "Filename");
}

/* Initializes the representation. */
const MVMREPROps * MVMCompUnit_initialize(MVMThreadContext *tc) {
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
    "MVMCompUnit", /* name */
    MVM_REPR_ID_MVMCompUnit,
    unmanaged_size,
    describe_refs,
};
