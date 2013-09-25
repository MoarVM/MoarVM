#include "moarvm.h"

/* stub type for now */

enum { PTR_ALIGN = offsetof(struct { char dummy; void *ptr; }, ptr) };

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMBlobBody *body = data;
    MVMuint64 *refmap = body->refmap;
    char *cursor, *end;
    MVMuint64 i;

    if (!refmap)
        return;

    cursor = body->storage;
    end    = body->storage + body->size;

    for (i = 0; cursor < end; i++) {
        MVMuint64 mask, word = refmap[i];

        if (!word) {
            cursor += 64 * PTR_ALIGN;
            continue;
        }

        for (mask = 1; mask; mask <<= 1, cursor += PTR_ALIGN)
            if (word & mask)
                MVM_gc_worklist_add(tc, worklist, cursor);
    }
}

static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMBlobBody *body = &((MVMBlob *)obj)->body;

    MVM_checked_free_null(body->storage);
    MVM_checked_free_null(body->refmap);
    body->size = 0;
}

static const MVMREPROps this_repr = {
    NULL, /* type_object_for, */
    NULL, /* allocate, */
    NULL, /* initialize */
    NULL, /* copy_to, */
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
    NULL, /* compose, */
    "VMBlob",
    MVM_REPR_ID_VMBlob,
    0, /* refs_frames */
};

const MVMREPROps * MVMBlob_initialize(MVMThreadContext *tc) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, NULL);

    MVMROOT(tc, st, {
        MVMObject *WHAT = MVM_gc_allocate_type_object(tc, st);
        tc->instance->raw_types.RawBlob = WHAT;
        MVM_ASSIGN_REF(tc, st, st->WHAT, WHAT);
        st->size = sizeof(MVMBlob);
    });

    MVM_gc_root_add_permanent(tc,
            (MVMCollectable **)&tc->instance->raw_types.RawBlob);

    return &this_repr;
}
