#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMHash_this_repr;

MVM_STATIC_INLINE MVMString * get_string_key(MVMThreadContext *tc, MVMObject *key) {
    if (MVM_UNLIKELY(!key || REPR(key)->ID != MVM_REPR_ID_MVMString || !IS_CONCRETE(key)))
        MVM_exception_throw_adhoc(tc, "MVMHash representation requires MVMString keys");
    return (MVMString *)key;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMHash_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMHash);
    });

    return st->WHAT;
}

#if MVM_HASH_PROTECT
static void try_read_lock(MVMThreadContext *tc, char *op, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
    if (tc->instance->hash_debug_enabled) {
        char *key = key_obj == NULL
                     ? "<nokey>"
                     : MVM_string_utf8_encode_C_string(tc, key_obj);
        fprintf(stderr, ">>> try read lock by '%s', key='%s' body=%p, LOCK %p\n", op, key, body, body->rw_lock);
        if (key_obj != NULL) MVM_free(key);
    }

    int code;
    if ((code = uv_rwlock_tryrdlock(body->rw_lock)) != 0) {
        fprintf(stderr, "Operation '%s' cannot obtain read lock for VMHash; error code=%d\n", op, code);
        MVM_dump_backtrace(tc);
        abort();
    }
}

static void try_write_lock(MVMThreadContext *tc, char *op, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
    if (tc->instance->hash_debug_enabled) {
        char *key = key_obj == NULL
                     ? "<nokey>"
                     : MVM_string_utf8_encode_C_string(tc, key_obj);
        fprintf(stderr, ">>> try write lock by '%s', key='%s' body=%p, LOCK %p\n", op, key, body, body->rw_lock);
        if (key_obj != NULL) MVM_free(key);
    }

    int code;
    if ((code = uv_rwlock_trywrlock(body->rw_lock)) != 0) {
        fprintf(stderr, "Operation '%s' cannot obtain write lock for VMHash; error code=%d\n", op, code);
        MVM_dump_backtrace(tc);
        abort();
    }
}

static void read_unlock(MVMThreadContext *tc, char *op, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
    if (tc->instance->hash_debug_enabled) {
        char *key = key_obj == NULL
                     ? "<nokey>"
                     : MVM_string_utf8_encode_C_string(tc, key_obj);
        fprintf(stderr, "<<< read unlock by '%s', key='%s' body=%p, LOCK %p\n", op, key, body, body->rw_lock);
        if (key_obj != NULL) MVM_free(key);
    }
    uv_rwlock_rdunlock(body->rw_lock);
}

static void write_unlock(MVMThreadContext *tc, char *op, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
    if (tc->instance->hash_debug_enabled) {
        char *key = key_obj == NULL
                     ? "<nokey>"
                     : MVM_string_utf8_encode_C_string(tc, key_obj);
        fprintf(stderr, "<<< write unlock by '%s', key='%s' body=%p, LOCK %p\n", op, key, body, body->rw_lock);
        if (key_obj != NULL) MVM_free(key);
    }
    uv_rwlock_wrunlock(body->rw_lock);
}

#endif

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashBody *src_body  = (MVMHashBody *)src;
    MVMHashBody *dest_body = (MVMHashBody *)dest;

    MVMStrHashTable *src_hashtable = &(src_body->hashtable);
    MVMStrHashTable *dest_hashtable = &(dest_body->hashtable);

#if MVM_HASH_PROTECT
    try_read_lock(tc, "copy_to", src, NULL);
    try_write_lock(tc, "copy_to", dest, NULL);
#endif

    if (dest_hashtable->table) {
        /* copy_to is, on reference types, only ever used as part of clone, and
         * that will always target a freshly created object.
         * So this should be unreachable. */
        MVM_oops(tc, "copy_to on MVMHash that is already initialized");
    }
    MVM_str_hash_shallow_copy(tc, src_hashtable, dest_hashtable);
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, dest_hashtable);
    while (!MVM_str_hash_at_end(tc, dest_hashtable, iterator)) {
        MVMHashEntry *entry = MVM_str_hash_current_nocheck(tc, dest_hashtable, iterator);
        MVM_gc_write_barrier(tc, &(dest_root->header), &(entry->value->header));
        MVM_gc_write_barrier(tc, &(dest_root->header), &(entry->hash_handle.key->common.header));
        iterator = MVM_str_hash_next_nocheck(tc, src_hashtable, iterator);
    }

#if MVM_HASH_PROTECT
    read_unlock(tc, "copy_to", src, NULL);
    write_unlock(tc, "copy_to", dest, NULL);
#endif
}

/* Adds held objects to the GC worklist. */
static void MVMHash_gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashBody     *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMuint64            elems = MVM_str_hash_count(tc, hashtable);

    /* Aren't holding anything, nothing to do. */
    if (elems == 0)
        return;

#if MVM_HASH_PROTECT
    try_read_lock(tc, "gc_mark", data, NULL);
#endif

    MVM_gc_worklist_presize_for(tc, worklist, 2 * elems);
    if (worklist->include_gen2) {
        MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
        while (!MVM_str_hash_at_end(tc, hashtable, iterator)) {
            MVMHashEntry *current = MVM_str_hash_current_nocheck(tc, hashtable, iterator);
            MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &current->hash_handle.key);
            MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &current->value);
            iterator = MVM_str_hash_next_nocheck(tc, hashtable, iterator);
        }
    }
    else {
        MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
        while (!MVM_str_hash_at_end(tc, hashtable, iterator)) {
            MVMHashEntry *current = MVM_str_hash_current_nocheck(tc, hashtable, iterator);
            MVMCollectable **key = (MVMCollectable **) &current->hash_handle.key;
            MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, key);
            MVM_gc_worklist_add_object_no_include_gen2_nocheck(tc, worklist, &current->value);
            iterator = MVM_str_hash_next_nocheck(tc, hashtable, iterator);
        }
    }
#if MVM_HASH_PROTECT
    read_unlock(tc, "gc_mark", data, NULL);
#endif
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHash *h = (MVMHash *)obj;
    MVMStrHashTable *hashtable = &(h->body.hashtable);

#if MVM_HASH_PROTECT
    if (tc->instance->hash_debug_enabled) {
        fprintf(stderr, "destroying RW-lock for %s|%llx of %s %p; lock=%p\n",
                obj->st->debug_name,
                MVM_gc_object_id(tc, obj),
                obj->st->REPR->name,
                &((MVMHash *)obj)->body,
                ((MVMHash *)obj)->body.rw_lock);
    }
    uv_rwlock_destroy(h->body.rw_lock);
    MVM_free(h->body.rw_lock);
#endif

    MVM_str_hash_demolish(tc, hashtable);
}

void MVMHash_at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister *result, MVMuint16 kind) {
    MVMHashBody   *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

#if MVM_HASH_PROTECT
    try_read_lock(tc, "at_key", data, key_obj);
#endif

    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");

    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, (MVMString *)key_obj);
    result->o = entry != NULL ? entry->value : tc->instance->VMNull;

#if MVM_HASH_PROTECT
    read_unlock(tc, "at_key", data, key_obj);
#endif
}

void MVMHash_bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister value, MVMuint16 kind) {
    MVMHashBody   *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

#if MVM_HASH_PROTECT
    try_write_lock(tc, "bind_key", data, key_obj);
#endif

    MVMString *key = (MVMString *)key_obj;
    if (!MVM_str_hash_key_is_valid(tc, key)) {
        MVM_str_hash_key_throw_invalid(tc, key);
    }
    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");

    if (!MVM_str_hash_entry_size(tc, hashtable)) {
        MVM_str_hash_build(tc, hashtable, sizeof(MVMHashEntry), 0);
    }

    MVMHashEntry *entry = MVM_str_hash_lvalue_fetch_nocheck(tc, hashtable, key);
    MVM_ASSIGN_REF(tc, &(root->header), entry->value, value.o);
    if (!entry->hash_handle.key) {
        entry->hash_handle.key = key;
        MVM_gc_write_barrier(tc, &(root->header), &(key->common.header));
    }

#if MVM_HASH_PROTECT
    write_unlock(tc, "bind_key", data, key_obj);
#endif
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;

#if MVM_HASH_PROTECT
    try_read_lock(tc, "elems", data, NULL);
#endif
    MVMuint64 elems = MVM_str_hash_count(tc, &(body->hashtable));
#if MVM_HASH_PROTECT
    read_unlock(tc, "elems", data, NULL);
#endif
    return elems;
}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj) {
    MVMHashBody   *body = (MVMHashBody *)data;
#if MVM_HASH_PROTECT
    try_read_lock(tc, "exists_key", data, key_obj);
#endif
    /* key_obj checked in MVM_str_hash_fetch */
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, (MVMString *)key_obj);

#if MVM_HASH_PROTECT
    read_unlock(tc, "exists_key", data, key_obj);
#endif
    return entry != NULL;
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
#if MVM_HASH_PROTECT
    try_write_lock(tc, "delete_key", data, key_obj);
#endif
    MVMString *key = get_string_key(tc, key_obj);
    MVMStrHashTable *hashtable = &(body->hashtable);

    MVM_str_hash_delete(tc, hashtable, key);

#if MVM_HASH_PROTECT
    write_unlock(tc, "delete_key", data, key_obj);
#endif
}

static MVMStorageSpec get_value_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;
    return spec;
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
    /* XXX key and value types will be communicated here */
}

/* Deserialize the representation. */
static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMHashBody *body = (MVMHashBody *)data;
#if MVM_HASH_PROTECT
    try_write_lock(tc, "deserialize", data, NULL);
#endif
    MVMStrHashTable *hashtable = &(body->hashtable);
    if (MVM_str_hash_entry_size(tc, hashtable)) {
        /* This should be unreachable. As clarified by @jnthn:
         *   The key question here is "what happens if the hash is repossessed".
         *   When that happens, the original one has this happen to it:
         *   [citing code in `repossess` in serialisation.c, which calls `memset`
         *   on the body.]
         * After that memset, MVM_str_hash_entry_size will return 0, hence this
         * code must be unreachable.] */
        MVM_oops(tc, "deserialize on MVMHash that is already initialized");
    }
    MVMint64 elems = MVM_serialization_read_int(tc, reader);
    if (elems) {
        MVM_str_hash_build(tc, hashtable, sizeof(MVMHashEntry), elems);
        MVMint64 i = 0;
        do {
            MVMString *key = MVM_serialization_read_str(tc, reader);
            if (!MVM_str_hash_key_is_valid(tc, key)) {
                MVM_str_hash_key_throw_invalid(tc, key);
            }
            MVMObject *value = MVM_serialization_read_ref(tc, reader);
            MVMHashEntry *entry = MVM_str_hash_insert_nocheck(tc, hashtable, key);
            MVM_ASSIGN_REF(tc, &(root->header), entry->value, value);
        } while (++i < elems);
    }
#if MVM_HASH_PROTECT
    write_unlock(tc, "deserialize", data, NULL);
#endif
}

/* Serialize the representation. */
static MVMThreadContext *cmp_tc;
static int cmp_strings(const void *s1, const void *s2) {
    return MVM_string_compare(cmp_tc, *(MVMString **)s1, *(MVMString **)s2);
}
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMHashBody *body = (MVMHashBody *)data;
#if MVM_HASH_PROTECT
    try_read_lock(tc, "serialize", data, NULL);
#endif
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMuint64 elems = MVM_str_hash_count(tc, hashtable);
    MVMString **keys = MVM_malloc(sizeof(MVMString *) * elems);
    MVMuint64 i = 0;
    MVM_serialization_write_int(tc, writer, elems);
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
    while (!MVM_str_hash_at_end(tc, hashtable, iterator)) {
        MVMHashEntry *current = MVM_str_hash_current_nocheck(tc, hashtable, iterator);
        keys[i++] = current->hash_handle.key;
        iterator = MVM_str_hash_next_nocheck(tc, hashtable, iterator);
    }
    cmp_tc = tc;
    qsort(keys, elems, sizeof(MVMString*), cmp_strings);
    for (i = 0; i < elems; i++) {
        MVMHashEntry *entry = MVM_str_hash_fetch_nocheck(tc, hashtable, keys[i]);
        MVM_serialization_write_str(tc, writer, keys[i]);
        MVM_serialization_write_ref(tc, writer, entry->value);
    }
    MVM_free(keys);
#if MVM_HASH_PROTECT
    read_unlock(tc, "serialize", data, NULL);
#endif
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMHash);
}

/* Bytecode specialization for this REPR. */
static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
#if !MVM_HASH_PROTECT
    switch (ins->info->opcode) {
    case MVM_OP_create: {
        if (!(st->mode_flags & MVM_FINALIZE_TYPE)) {
            MVMSpeshOperand target   = ins->operands[0];
            MVMSpeshOperand type     = ins->operands[1];
            MVMSpeshFacts *tgt_facts = MVM_spesh_get_facts(tc, g, target);

            ins->info                = MVM_op_get_op(MVM_OP_sp_fastcreate);
            ins->operands            = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0]         = target;
            ins->operands[1].lit_i16 = sizeof(MVMHash);
            ins->operands[2].lit_i16 = MVM_spesh_add_spesh_slot(tc, g, (MVMCollectable *)st);
            MVM_spesh_usages_delete_by_reg(tc, g, type, ins);

            tgt_facts->flags |= MVM_SPESH_FACT_KNOWN_TYPE | MVM_SPESH_FACT_CONCRETE;
            tgt_facts->type = st->WHAT;
        }
        break;
    }
    }
#endif
}

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;

    return MVM_str_hash_allocated_size(tc, &(body->hashtable));
}

#if MVM_HASH_PROTECT
static MVMObject * gc_allocate(MVMThreadContext *tc, MVMSTable *st) {
    MVMObject *obj = MVM_gc_allocate_object(tc, st);
    uv_rwlock_t *rw_lock = MVM_malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(rw_lock);
    ((MVMHash *)obj)->body.rw_lock = rw_lock;
    if (tc->instance->hash_debug_enabled) {
        fprintf(stderr, "initialized RW-lock %s|%llx of %s %p; lock=%p\n",
                obj->st->debug_name,
                MVM_gc_object_id(tc, obj),
                obj->st->REPR->name,
                &((MVMHash *)obj)->body,
                rw_lock);
    }
    return obj;
}
#endif

/* Initializes the representation. */
const MVMREPROps * MVMHash_initialize(MVMThreadContext *tc) {
    return &MVMHash_this_repr;
}

static const MVMREPROps MVMHash_this_repr = {
    type_object_for,
#if MVM_HASH_PROTECT
    gc_allocate, /* serialization.c relies on this and the next line */
#else
    MVM_gc_allocate_object, /* serialization.c relies on this and the next line */
#endif
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    {
        MVMHash_at_key,
        MVMHash_bind_key,
        exists_key,
        delete_key,
        get_value_storage_spec
    },    /* ass_funcs */
    elems,
    get_storage_spec,
    NULL, /* change_type */
    serialize,
    deserialize,
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    MVMHash_gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    spesh,
    "VMHash", /* name */
    MVM_REPR_ID_MVMHash,
    unmanaged_size, /* unmanaged_size */
    NULL, /* describe_refs */
};
