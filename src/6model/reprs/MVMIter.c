#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMIter);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMIterBody *src_body  = (MVMIterBody *)src;
    MVMIterBody *dest_body = (MVMIterBody *)dest;
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation VMIter");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMIterBody  *body  = (MVMIterBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->target);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMIter *iter = (MVMIter *)obj;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

static void at_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister *value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void bind_pos(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 index, MVMRegister value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void push(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void pop(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void unshift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVM_exception_throw_adhoc(tc, "Invalid operation on iterator");
}

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVMObject *target = body->target;
    switch (body->mode) {
        case MVM_ITER_MODE_ARRAY:
            body->array_state.index++;
            if (body->array_state.index >= body->array_state.limit)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            REPR(target)->pos_funcs->at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, value, kind);
            return;
        case MVM_ITER_MODE_HASH:
            if (!body->hash_state.curr) {
                if (body->hash_state.next) {
                    body->hash_state.curr = body->hash_state.next;
                    body->hash_state.next = body->hash_state.next->hash_handle.next;
                }
                else {
                    MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
                }
            }
            else {
                body->hash_state.curr = body->hash_state.curr->hash_handle.next;
                body->hash_state.next = body->hash_state.curr->hash_handle.next;
            }
            value->o = root;
            return;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown iteration mode");
    }
}

/* This whole splice optimization can be optimized for the case we have two
 * MVMIter representation objects. */
static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
    MVMIterBody *body = (MVMIterBody *)data;
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* XXX element type supplied through this... */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMIter);
}

/* Initializes the representation. */
MVMREPROps * MVMIter_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps_Positional pos_funcs = {
    at_pos,
    bind_pos,
    set_elems,
    NULL, /* exists_pos */
    push,
    pop,
    unshift,
    shift,
    splice,
    get_elem_storage_spec
};

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    NULL, /* initialize */
    copy_to,
    &MVM_REPR_DEFAULT_ATTR_FUNCS,
    &MVM_REPR_DEFAULT_BOX_FUNCS,
    &pos_funcs,
    &MVM_REPR_DEFAULT_ASS_FUNCS,
    elems,
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
    "VMIter", /* name */
    MVM_REPR_ID_MVMIter,
    0, /* refs_frames */
};

MVMObject * MVM_iter(MVMThreadContext *tc, MVMObject *target) {
    MVMIter *iterator;
    MVMROOT(tc, target, {
        if (REPR(target)->ID == MVM_REPR_ID_MVMArray) {
            iterator = (MVMIter *)MVM_repr_alloc_init(tc,
                MVM_hll_current(tc)->array_iterator_type);
            iterator->body.mode = MVM_ITER_MODE_ARRAY;
            iterator->body.array_state.index = -1;
            iterator->body.array_state.limit = REPR(target)->elems(tc, STABLE(target), target, OBJECT_BODY(target));
            MVM_ASSIGN_REF(tc, iterator, iterator->body.target, target);
        }
        else if (REPR(target)->ID == MVM_REPR_ID_MVMHash) {
            iterator = (MVMIter *)MVM_repr_alloc_init(tc,
                MVM_hll_current(tc)->hash_iterator_type);
            iterator->body.mode = MVM_ITER_MODE_HASH;
            iterator->body.hash_state.next = ((MVMHash *)target)->body.hash_head;
            MVM_ASSIGN_REF(tc, iterator, iterator->body.target, target);
        }
        else if (REPR(target)->ID == MVM_REPR_ID_MVMContext) {
            /* Turn the context into a VMHash and then iterate that. */
            MVMObject *ctx_hash = MVM_repr_alloc_init(tc,
                MVM_hll_current(tc)->slurpy_hash_type);
            MVMROOT(tc, ctx_hash, {
                MVMContext *ctx = (MVMContext *)target;
                MVMFrame *frame = ctx->body.context;
                MVMLexicalRegistry *lexical_names = frame->static_info->body.lexical_names;
                MVMLexicalRegistry *current;
                MVMLexicalRegistry *tmp;
                HASH_ITER(hash_handle, lexical_names, current, tmp) {
                    /* XXX For now, just the symbol names is enough. */
                    MVM_repr_bind_key_boxed(tc, ctx_hash, (MVMString *)current->key, NULL);
                }
            });

            /* Call ourselves recursively to get the iterator for this
            * hash. */
            iterator = (MVMIter *)MVM_iter(tc, ctx_hash);
        }
        else {
            MVM_exception_throw_adhoc(tc, "Cannot iterate this");
        }
    });
    return (MVMObject *)iterator;
}

MVMint64 MVM_iter_istrue(MVMThreadContext *tc, MVMIter *iter) {
    switch (iter->body.mode) {
        case MVM_ITER_MODE_ARRAY:
            return iter->body.array_state.index + 1 < iter->body.array_state.limit ? 1 : 0;
            break;
        case MVM_ITER_MODE_HASH:
            return iter->body.hash_state.next != NULL ? 1 : 0;
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Invalid iteration mode used");
    }
}

MVMString * MVM_iterkey_s(MVMThreadContext *tc, MVMIter *iterator) {
    if (REPR(iterator)->ID != MVM_REPR_ID_MVMIter
            || iterator->body.mode != MVM_ITER_MODE_HASH)
        MVM_exception_throw_adhoc(tc, "This is not a hash iterator");
    if (!iterator->body.hash_state.curr)
        MVM_exception_throw_adhoc(tc, "You have not advanced to the first item of the hash iterator, or have gone past the end");
    return (MVMString *)iterator->body.hash_state.curr->key;
}

MVMObject * MVM_iterval(MVMThreadContext *tc, MVMIter *iterator) {
    MVMIterBody *body;
    MVMObject *target;
    MVMRegister result;
    if (REPR(iterator)->ID != MVM_REPR_ID_MVMIter)
        MVM_exception_throw_adhoc(tc, "This is not an iterator");
    if (iterator->body.mode == MVM_ITER_MODE_ARRAY) {
        body = &iterator->body;
        if (body->array_state.index == -1)
            MVM_exception_throw_adhoc(tc, "You have not yet advanced in the array iterator");
        target = body->target;
        REPR(target)->pos_funcs->at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &result, MVM_reg_obj);
    }
    else if (iterator->body.mode == MVM_ITER_MODE_HASH) {
        if (!iterator->body.hash_state.curr)
        MVM_exception_throw_adhoc(tc, "You have not advanced to the first item of the hash iterator, or have gone past the end");
        result.o = iterator->body.hash_state.curr->value;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Unknown iterator mode in iterval");
    }
    return result.o;
}
