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
        st->size = sizeof(MVMPtr);
    });

    return st->WHAT;
}

static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data) {
    MVMPtrBody *body = data;

    if (!st->REPR_data)
        MVM_exception_throw_adhoc(tc,
                "cannot initialize C array from uncomposed type object");

    body->cobj = NULL;
    body->blob = NULL;
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMPtrBody *body = data;
    MVMCArraySpec *spec = st->REPR_data;
    MVMObject *type = spec->elem_type;

    if (index < 0 || index >= spec->elem_count)
        MVM_exception_throw_adhoc(tc, "C array index out of bounds");

    switch (kind) {
        case MVM_reg_obj:
            MVMROOT(tc, root, {
                MVMPtr *ptr = (MVMPtr *)MVM_repr_alloc_init(tc, type);
                ptr->body.cobj = (char *)body->cobj + index * spec->elem_size;
                MVM_ASSIGN_REF(tc, ptr, ptr->body.blob, body->blob);
                value->o = (MVMObject *)ptr;
            });
            return;

        case MVM_reg_int64: {
            MVMPtrBody dummy = {
                (char *)body->cobj + index * spec->elem_size, NULL
            };

            value->i64 = REPR(type)->box_funcs.get_int(tc, STABLE(type), root,
                    &dummy);
            return;
        }

        case MVM_reg_num64: {
            MVMPtrBody dummy = {
                (char *)body->cobj + index * spec->elem_size,  NULL
            };

            value->n64 = REPR(type)->box_funcs.get_num(tc, STABLE(type), root,
                    &dummy);
            return;
        }

        default:
            MVM_exception_throw_adhoc(tc,
                    "unsupported result kind for C array at_pos");
    }
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMPtrBody *body = data;
    MVMCArraySpec *spec = st->REPR_data;
    MVMObject *type = spec->elem_type;

    if (index < 0 || index >= spec->elem_count)
        MVM_exception_throw_adhoc(tc, "C array index out of bounds");

    switch (kind) {
        case MVM_reg_obj:
            MVM_exception_throw_adhoc(tc, "TODO [%s:%u]", __FILE__, __LINE__);

        case MVM_reg_int64: {
            MVMPtrBody dummy = {
                (char *)body->cobj + index * spec->elem_size, NULL
            };

            REPR(type)->box_funcs.set_int(tc, STABLE(type), root,
                    &dummy, value.i64);
            return;
        }

        case MVM_reg_num64: {
            MVMPtrBody dummy = {
                (char *)body->cobj + index * spec->elem_size, NULL
            };

            REPR(type)->box_funcs.set_num(tc, STABLE(type), root,
                    &dummy, value.n64);
            return;
        }

        default: fail:
            MVM_exception_throw_adhoc(tc,
                    "unsupported argument kind for C array bind_pos");
    }
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMuint64 count) {
    MVM_exception_throw_adhoc(tc, "cannot resize C array");
}

static MVMint64 exists_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMint64 index) {
    return 0 <= index && index < ((MVMCArraySpec *)st->REPR_data)->elem_count;
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
    return ((MVMCArraySpec *)st->REPR_data)->elem_count;
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
    MVMPtrBody *body = data;

    if (body->blob)
        MVM_gc_worklist_add(tc, worklist, &body->blob);
}

static void gc_mark_repr_data(MVMThreadContext *tc, MVMSTable *st,
        MVMGCWorklist *worklist) {
    MVMCArraySpec *spec = st->REPR_data;

    if (spec)
        MVM_gc_worklist_add(tc, worklist, &spec->elem_type);
}

static void gc_free_repr_data(MVMThreadContext *tc, MVMSTable *st) {
    MVM_checked_free_null(st->REPR_data);
}

static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    MVMCArraySpec *spec;
    MVMObject *type  = MVM_repr_at_pos_o(tc, info, 0);
    MVMObject *count = MVM_repr_at_pos_o(tc, info, 1);

    spec = malloc(sizeof *spec);
    spec->elem_count = MVM_repr_get_int(tc, count);;
    spec->elem_size  = MVM_native_csizeof(tc, type);
    MVM_ASSIGN_REF(tc, st, spec->elem_type, type);

    st->REPR_data = spec;
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
    gc_free_repr_data,
    compose,
    "CArray",
    MVM_REPR_ID_CArray,
    0, /* refs_frames */
};
