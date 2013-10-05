#include "moarvm.h"

enum { PTR_ALIGN = offsetof(struct { char dummy; void *ptr; }, ptr) };

static const MVMREPROps this_repr;

const MVMREPROps * MVMMemBlob_initialize(MVMThreadContext *tc) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, NULL);

    MVMROOT(tc, st, {
        MVMObject *WHAT = MVM_gc_allocate_type_object(tc, st);
        tc->instance->raw_types.RawMemBlob = WHAT;
        MVM_ASSIGN_REF(tc, st, st->WHAT, WHAT);
        st->size = sizeof(MVMMemBlob);
    });

    MVM_gc_root_add_permanent(tc,
            (MVMCollectable **)&tc->instance->raw_types.RawMemBlob);

    return &this_repr;
}

static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMMemBlob);
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    ((MVMMemBlobBody *)data)->address = NULL;
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 value) {
    MVM_exception_throw_adhoc(tc, "cannot set address of memory blob");
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    return (MVMint64)((MVMMemBlobBody *)data)->address;
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = MVM_STORAGE_SPEC_CAN_BOX_INT;

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMMemBlobBody *body = data;
    MVMuint64 *refmap = body->refmap;
    char *cursor, *end;
    MVMuint64 i;

    if (!refmap)
        return;

    cursor = body->address;
    end    = cursor + body->size;

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
    MVMMemBlobBody *body = &((MVMMemBlob *)obj)->body;

    MVM_checked_free_null(body->address);
    MVM_checked_free_null(body->refmap);
    body->size = 0;
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* noop */
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    NULL, /* copy_to */
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    { /* box_funcs */
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        MVM_REPR_DEFAULT_GET_BOXED_REF,
    },
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
    "MVMMemBlob",
    MVM_REPR_ID_MVMMemBlob,
    0, /* refs_frames */
};
