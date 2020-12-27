#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMIter_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMIter_this_repr, HOW);

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
            ; /* Sigh, C99 won't let me put a declaration here. */
            MVMStrHashTable *hashtable = &(((MVMHash *)target)->body.hashtable);
#if HASH_DEBUG_ITER
            struct MVMStrHashTableControl *control = hashtable->table;
            MVMuint64 ht_id = control ? control->ht_id : 0;
            if (body->hash_state.curr.owner != ht_id) {
                MVM_oops(tc, "MVMIter shift called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                         body->hash_state.curr.owner, ht_id);
            }
            /* OK, to implement "delete at current iterator position" we need
             * to cheat somewhat. */
            if (MVM_str_hash_iterator_target_deleted(tc, hashtable, body->hash_state.curr)) {
                /* The only action taken on the hash was to delete at the
                 * current iterator. In which case, the "next" iterator is
                 * valid (but has already been advanced beyond pos, so we
                 * can't perform this test on it). So "fix up" its state to pass
                 * muster with the HASH_DEBUG_ITER sanity tests. */
                body->hash_state.next.serial = control->serial;
            }
#endif
            body->hash_state.curr = body->hash_state.next;
            if (MVM_str_hash_at_end(tc, hashtable, body->hash_state.curr))
                MVM_exception_throw_adhoc(tc, "Iteration past end of iterator");
            body->hash_state.next = MVM_str_hash_next_nocheck(tc, hashtable, body->hash_state.curr);
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
    return &MVMIter_this_repr;
}

static const MVMREPROps MVMIter_this_repr = {
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
        MVM_REPR_DEFAULT_PUSH,
        MVM_REPR_DEFAULT_POP,
        MVM_REPR_DEFAULT_UNSHIFT,
        shift,
        MVM_REPR_DEFAULT_SLICE,
        isplice,
        MVM_REPR_DEFAULT_AT_POS_MULTIDIM,
        MVM_REPR_DEFAULT_BIND_POS_MULTIDIM,
        MVM_REPR_DEFAULT_DIMENSIONS,
        MVM_REPR_DEFAULT_SET_DIMENSIONS,
        get_elem_storage_spec,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC,
        MVM_REPR_DEFAULT_POS_AS_ATOMIC_MULTIDIM,
        MVM_REPR_DEFAULT_POS_WRITE_BUF,
        MVM_REPR_DEFAULT_POS_READ_BUF
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
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

MVMObject * MVM_iter(MVMThreadContext *tc, MVMObject *target) {
    MVMIter *iterator;
    if (!IS_CONCRETE(target)) {
        MVM_exception_throw_adhoc(tc, "Cannot iterate over a %s type object", MVM_6model_get_debug_name(tc, target));
    }
    MVMROOT(tc, target, {
        if (REPR(target)->ID == MVM_REPR_ID_VMArray) {
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
            MVMStrHashTable *hashtable = &(((MVMHash *)target)->body.hashtable);
            iterator->body.hash_state.curr = MVM_str_hash_start(tc, hashtable);
            iterator->body.hash_state.next = MVM_str_hash_first(tc, hashtable);
            MVM_ASSIGN_REF(tc, &(iterator->common.header), iterator->body.target, target);
        }
        else if (REPR(target)->ID == MVM_REPR_ID_MVMContext) {
            /* Turn the context into a VMHash and then iterate that. */
            MVMObject *ctx_hash = MVM_context_lexicals_as_hash(tc, (MVMContext *)target);
            iterator = (MVMIter *)MVM_iter(tc, ctx_hash);
        }
        else {
            MVM_exception_throw_adhoc(tc, "Cannot iterate object with %s representation (%s)",
                REPR(target)->name, MVM_6model_get_debug_name(tc, target));
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
            return MVM_iter_istrue_array(tc, iter);
            break;
        case MVM_ITER_MODE_HASH:
            return MVM_iter_istrue_hash(tc, iter);
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Invalid iteration mode used");
    }
}

MVMString * MVM_iterkey_s(MVMThreadContext *tc, MVMIter *iterator) {
    if (REPR(iterator)->ID != MVM_REPR_ID_MVMIter
            || iterator->body.mode != MVM_ITER_MODE_HASH)
        MVM_exception_throw_adhoc(tc, "This is not a hash iterator, it's a %s (%s)", REPR(iterator)->name, MVM_6model_get_debug_name(tc, (MVMObject *)iterator));

    MVMStrHashTable *hashtable = &(((MVMHash *)iterator->body.target)->body.hashtable);

#if HASH_DEBUG_ITER
    struct MVMStrHashTableControl *control = hashtable->table;
    MVMuint64 ht_id = control ? control->ht_id : 0;
    if (iterator->body.hash_state.next.owner != ht_id) {
        MVM_oops(tc, "MVM_itereky_s called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator->body.hash_state.next.owner, ht_id);
    }
#endif

    if (MVM_str_hash_at_end(tc, hashtable, iterator->body.hash_state.curr)
        || MVM_str_hash_at_start(tc, hashtable, iterator->body.hash_state.curr))
        MVM_exception_throw_adhoc(tc, "You have not advanced to the first item of the hash iterator, or have gone past the end");

    struct MVMHashEntry *entry = MVM_str_hash_current_nocheck(tc, hashtable, iterator->body.hash_state.curr);
    return entry->hash_handle.key;
}

MVMObject * MVM_iterval(MVMThreadContext *tc, MVMIter *iterator) {
    MVMIterBody *body;
    MVMObject *target;
    MVMRegister result;
    if (REPR(iterator)->ID != MVM_REPR_ID_MVMIter)
        MVM_exception_throw_adhoc(tc, "This is not an iterator, it's a %s (%s)", REPR(iterator)->name, MVM_6model_get_debug_name(tc, (MVMObject *)iterator));
    if (iterator->body.mode == MVM_ITER_MODE_ARRAY) {
        body = &iterator->body;
        if (body->array_state.index == -1)
            MVM_exception_throw_adhoc(tc, "You have not yet advanced in the array iterator");
        target = body->target;
        REPR(target)->pos_funcs.at_pos(tc, STABLE(target), target, OBJECT_BODY(target), body->array_state.index, &result, MVM_reg_obj);
    }
    else if (iterator->body.mode == MVM_ITER_MODE_HASH) {
        MVMStrHashTable *hashtable = &(((MVMHash *)iterator->body.target)->body.hashtable);

#if HASH_DEBUG_ITER
        struct MVMStrHashTableControl *control = hashtable->table;
        MVMuint64 ht_id = control ? control->ht_id : 0;
        if (iterator->body.hash_state.next.owner != ht_id) {
        MVM_oops(tc, "MVM_iterval called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator->body.hash_state.next.owner, ht_id);
        }
#endif

        if (MVM_str_hash_at_end(tc, hashtable, iterator->body.hash_state.curr)
            || MVM_str_hash_at_start(tc, hashtable, iterator->body.hash_state.curr))
            MVM_exception_throw_adhoc(tc, "You have not advanced to the first item of the hash iterator, or have gone past the end");
        struct MVMHashEntry *entry = MVM_str_hash_current_nocheck(tc, hashtable, iterator->body.hash_state.curr);
        result.o = entry->value;
        if (!result.o)
            result.o = tc->instance->VMNull;
    }
    else {
        MVM_exception_throw_adhoc(tc, "Unknown iterator mode in iterval");
    }
    return result.o;
}
