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
        MVMint32 i, j;
        for (i = 0; i < body->num_spesh_candidates; i++) {
            for (j = 0; j < body->spesh_candidates[i]->num_spesh_slots; j++)
                MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i]->spesh_slots[j]);
            for (j = 0; j < body->spesh_candidates[i]->num_inlines; j++)
                MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i]->inlines[j].sf);
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMStaticFrameSpesh *sfs = (MVMStaticFrameSpesh *)obj;
    MVMint32 i;
    MVM_spesh_stats_destroy(tc, sfs->body.spesh_stats);
    MVM_spesh_arg_guard_destroy(tc, sfs->body.spesh_arg_guard, 0);
    for (i = 0; i < sfs->body.num_spesh_candidates; i++)
        MVM_spesh_candidate_destroy(tc, sfs->body.spesh_candidates[i]);
    if (sfs->body.spesh_candidates)
        MVM_fixed_size_free(tc, tc->instance->fsa,
            sfs->body.num_spesh_candidates * sizeof(MVMSpeshCandidate *),
            sfs->body.spesh_candidates);
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

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMStaticFrameSpeshBody *body = (MVMStaticFrameSpeshBody *)data;
    MVMuint64 size = 0;
    MVMuint32 spesh_idx;
    for (spesh_idx = 0; spesh_idx < body->num_spesh_candidates; spesh_idx++) {
        MVMSpeshCandidate *cand = body->spesh_candidates[spesh_idx];

        size += cand->bytecode_size;

        size += sizeof(MVMFrameHandler) * cand->num_handlers;

        size += sizeof(MVMCollectable *) * cand->num_spesh_slots;

        size += sizeof(MVMint32) * cand->num_deopts;

        size += sizeof(MVMSpeshInline) * cand->num_inlines;

        size += sizeof(MVMuint16) * (cand->num_locals + cand->num_lexicals);

        /* XXX probably don't need to measure the bytecode size here,
         * as it's probably just a pointer to the same bytecode we have in
         * the static frame anyway. */

        /* Dive into the jit code */
        if (cand->jitcode) {
            MVMJitCode *code = cand->jitcode;

            size += sizeof(MVMJitCode);

            size += sizeof(void *) * code->num_labels;

            size += sizeof(MVMint32) * code->num_bbs;
            size += sizeof(MVMJitDeopt) * code->num_deopts;
            size += sizeof(MVMJitInline) * code->num_inlines;
            size += sizeof(MVMJitHandler) * code->num_handlers;
        }
    }
    return size;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMStaticFrameSpeshBody *body = (MVMStaticFrameSpeshBody *)data;
    if (body->num_spesh_candidates) {
        MVMint32 i, j;
        for (i = 0; i < body->num_spesh_candidates; i++) {
            for (j = 0; j < body->spesh_candidates[i]->num_spesh_slots; j++)
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)body->spesh_candidates[i]->spesh_slots[j],
                    "Spesh slot entry");
            for (j = 0; j < body->spesh_candidates[i]->num_inlines; j++)
                MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss,
                    (MVMCollectable *)body->spesh_candidates[i]->inlines[j].sf,
                    "Spesh inlined static frame");
        }
    }
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
    unmanaged_size,
    describe_refs
};
