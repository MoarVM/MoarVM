#include "moarvm.h"

static const MVMREPROps this_repr;

const MVMREPROps * MVMCArray_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMCArray);
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVMCArrayBody *body = data;

    if (!st->REPR_data)
        MVM_exception_throw_adhoc(tc,
                "cannot initialize C array from uncomposed type object");

    body->cobj = NULL;
    body->blob = NULL;
    body->elem_count = 0;
    body->elem_size = MVM_native_csizeof(tc, st->REPR_data);
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMCArrayBody *body = data;

    if (index < 0 || index >= body->elem_count)
        MVM_exception_throw_adhoc(tc, "C array index out of bounds");

    switch (kind) {
        case MVM_reg_obj:
            MVMROOT(tc, root, {
                MVMPtr *ptr = (MVMPtr *)MVM_repr_alloc_init(tc, st->REPR_data);
                ptr->body.cobj = (char *)body->cobj + index * body->elem_size;
                MVM_ASSIGN_REF(tc, ptr, ptr->body.blob, body->blob);
                value->o = (MVMObject *)ptr;
            });
            return;

        case MVM_reg_int64:
        case MVM_reg_num64:
            break;

        default:
            MVM_exception_throw_adhoc(tc,
                    "unsupported result kind for C array at_pos");
    }

    MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMCArrayBody *body = data;

    if (index < 0 || index >= body->elem_count)
        MVM_exception_throw_adhoc(tc, "C array index out of bounds");

    switch (kind) {
        case MVM_reg_obj:
            MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);

        case MVM_reg_int64:
        case MVM_reg_num64:
            break;

        default:
            MVM_exception_throw_adhoc(tc,
                    "unsupported argument kind for C array bind_pos");
    }

    MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMuint64 count) {
    MVMCArrayBody *body = data;

    if (body->elem_count != 0)
        MVM_exception_throw_adhoc(tc, "cannot resize C array");

    if (count == 0)
        MVM_exception_throw_adhoc(tc, "C array size must be non-zero");

    if (body->blob) {
        MVMBlobBody *blob_body = &body->blob->body;

        if ((char *)body->cobj + body->elem_size * count
                > blob_body->storage + blob_body->size)
            MVM_exception_throw_adhoc(tc, "blob overflow");
    }

    body->elem_count = count;
}

static MVMint64 exists_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index) {
    MVMCArrayBody *body = data;
    return 0 <= index && index < body->elem_count;
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMRegister value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "cannot push to C array");
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMRegister *value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "cannot pop from C array");
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMRegister value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "cannot unshift to C array");
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMRegister *value, MVMuint16 kind) {
    MVM_exception_throw_adhoc(tc, "cannot shift from C array");
}

static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
    MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc,
        MVMSTable *st) {
    MVMObject *type = st->REPR_data;

    if (!type)
        MVM_exception_throw_adhoc(tc, "C array type not composed");

    return REPR(type)->get_storage_spec(tc, STABLE(type));
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    return ((MVMCArrayBody *)data)->elem_count;
}

static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;

    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;

    return spec;
}

static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data,
        MVMGCWorklist *worklist) {
    MVMCArrayBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    if (st->REPR_data)
        MVM_gc_worklist_add(tc, worklist, &st->REPR_data);
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    MVM_ASSIGN_REF(tc, st, st->REPR_data, info);
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    NULL, /* copy_to */
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    { /* pos_funcs */
        at_pos,
        bind_pos,
        set_elems,
        exists_pos,
        push,
        pop,
        unshift,
        shift,
        splice,
        get_elem_storage_spec
    },
    MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    gc_mark_repr_data,
    NULL, /* gc_free_repr_data */
    compose,
    "CArray",
    MVM_REPR_ID_CArray,
    0, /* refs_frames */
};
