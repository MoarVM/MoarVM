#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMIter);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation VMIter");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMIterBody  *body  = (MVMIterBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->target);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
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

static void shift(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMRegister *value, MVMuint16 kind) {
    MVMIterBody *body = (MVMIterBody *)data;
    MVMObject *target = body->target;
    switch (body->mode) {
        case MVM_ITER_MODE_ARRAY:
            body->array_state.index++;
            if (body->array_state.index >= body->array_state.limit)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, value, kind);
            return;
        case MVM_ITER_MODE_ARRAY_INT:
            body->array_state.index++;
            if (body->array_state.index >= body->array_state.limit)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            if (kind == MVM_reg_int64) {
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, value, kind);
            }
            else if (kind == MVM_reg_obj) {
                MVMRegister tmp;
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &tmp, MVM_reg_int64);
                value->o = MVM_repr_box_int(tc, MVM_hll_current(tc)->int_box_type, tmp.i64);
            }
            else {
                MVM_exception_throw_adhoc(tc, "Wrong register kind in iteration");
            }
            return;
        case MVM_ITER_MODE_ARRAY_NUM:
            body->array_state.index++;
            if (body->array_state.index >= body->array_state.limit)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            if (kind == MVM_reg_num64) {
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, value, kind);
            }
            else if (kind == MVM_reg_obj) {
                MVMRegister tmp;
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &tmp, MVM_reg_num64);
                value->o = MVM_repr_box_num(tc, MVM_hll_current(tc)->num_box_type, tmp.n64);
            }
            else {
                MVM_exception_throw_adhoc(tc, "Wrong register kind in iteration");
            }
            return;
        case MVM_ITER_MODE_ARRAY_STR:
            body->array_state.index++;
            if (body->array_state.index >= body->array_state.limit)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            if (kind == MVM_reg_str) {
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, value, kind);
            }
            else if (kind == MVM_reg_obj) {
                MVMRegister tmp;
                REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &tmp, MVM_reg_str);
                value->o = MVM_repr_box_str(tc, MVM_hll_current(tc)->str_box_type, tmp.s);
            }
            else {
                MVM_exception_throw_adhoc(tc, "Wrong register kind in iteration");
            }
            return;
        case MVM_ITER_MODE_HASH:
            body->hash_state.curr = body->hash_state.next;
            if (!body->hash_state.curr)
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            body->hash_state.next = HASH_ITER_NEXT_ITEM(
                &(body->hash_state.next->hash_handle),
                &(body->hash_state.bucket_state));
            value->o = root;
            return;
        default:
            MVM_exception_throw_adhoc(tc, "Unknown iteration mode");
    }
}

/* This whole splice optimization can be optimized for the case we have two
 * MVMIter representation objects. */
static void isplice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *from, MVMint64 offset, MVMuint64 count) {
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;
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
const MVMREPROps * MVMIter_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    {
        MVM_REPR_DEFAULT_AT_POS,
        MVM_REPR_DEFAULT_BIND_POS,
        MVM_REPR_DEFAULT_SET_ELEMS,
        MVM_REPR_DEFAULT_EXISTS_POS,
        MVM_REPR_DEFAULT_PUSH,
        MVM_REPR_DEFAULT_POP,
        MVM_REPR_DEFAULT_UNSHIFT,
        shift,
        isplice,
        get_elem_storage_spec
    },    /* pos_funcs */
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
            iterator->body.array_state.index = -1;
            iterator->body.array_state.limit = REPR(target)->elems(tc, STABLE(target), target, OBJECT_BODY(target));
            MVM_ASSIGN_REF(tc, &(iterator->common.header), iterator->body.target, target);
            switch (REPR(target)->pos_funcs.get_elem_storage_spec(tc, STABLE(target)).boxed_primitive) {
                case MVM_STORAGE_SPEC_BP_INT: iterator->body.mode = MVM_ITER_MODE_ARRAY_INT; break;
                case MVM_STORAGE_SPEC_BP_NUM: iterator->body.mode = MVM_ITER_MODE_ARRAY_NUM; break;
                case MVM_STORAGE_SPEC_BP_STR: iterator->body.mode = MVM_ITER_MODE_ARRAY_STR; break;
                default:                      iterator->body.mode = MVM_ITER_MODE_ARRAY; break;
            }
        }
        else if (REPR(target)->ID == MVM_REPR_ID_MVMHash) {
            iterator = (MVMIter *)MVM_repr_alloc_init(tc,
                MVM_hll_current(tc)->hash_iterator_type);
            iterator->body.mode = MVM_ITER_MODE_HASH;
            iterator->body.hash_state.bucket_state = 0;
            iterator->body.hash_state.curr         = NULL;
            iterator->body.hash_state.next         = HASH_ITER_FIRST_ITEM(
                ((MVMHash *)target)->body.hash_head
                    ? ((MVMHash *)target)->body.hash_head->hash_handle.tbl
                    : NULL,
                &(iterator->body.hash_state.bucket_state));
            MVM_ASSIGN_REF(tc, &(iterator->common.header), iterator->body.target, target);
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
                unsigned bucket_tmp;
                HASH_ITER(hash_handle, lexical_names, current, tmp, bucket_tmp) {
                    /* XXX For now, just the symbol names is enough. */
                    MVM_repr_bind_key_o(tc, ctx_hash, (MVMString *)current->key, NULL);
                }
            });

            /* Call ourselves recursively to get the iterator for this
            * hash. */
            iterator = (MVMIter *)MVM_iter(tc, ctx_hash);
        }
        else {
            MVM_exception_throw_adhoc(tc, "Cannot iterate object with %s representation",
                REPR(target)->name);
        }
    });
    return (MVMObject *)iterator;
}

MVMint64 MVM_iter_istrue(MVMThreadContext *tc, MVMIter *iter) {
    switch (iter->body.mode) {
        case MVM_ITER_MODE_ARRAY:
        case MVM_ITER_MODE_ARRAY_INT:
        case MVM_ITER_MODE_ARRAY_NUM:
        case MVM_ITER_MODE_ARRAY_STR:
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
        REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &result, MVM_reg_obj);
    }
    else if (iterator->body.mode == MVM_ITER_MODE_HASH) {
        if (!iterator->body.hash_state.curr)
        MVM_exception_throw_adhoc(tc, "You have not advanced to the first item of the hash iterator, or have gone past the end");
        result.o = iterator->body.hash_state.curr->value;
        if (!result.o)
            result.o = tc->instance->VMNull;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Unknown iterator mode in iterval");
    }
    return result.o;
}
