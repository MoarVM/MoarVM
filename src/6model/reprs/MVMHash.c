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

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashBody *src_body  = (MVMHashBody *)src;
    MVMHashBody *dest_body = (MVMHashBody *)dest;

    MVMStrHashTable *src_hashtable = &(src_body->hashtable);
    MVMStrHashTable *dest_hashtable = &(dest_body->hashtable);
    if (MVM_str_hash_entry_size(dest_hashtable)) {
        // XXX Is this a valid assumption?
        MVM_oops(tc, "copy_to on MVMHash that is already initialized");
    }
    MVM_str_hash_build(tc, dest_hashtable, sizeof(MVMHashEntry));
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, src_hashtable);
    MVMHashEntry *entry;
    while ((entry = MVM_str_hash_current(tc, src_hashtable, iterator))) {
        MVMHashEntry *new_entry = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMHashEntry));
        MVMString *key = entry->hash_handle.key;
        new_entry->hash_handle.key = key;
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->value, entry->value);
        MVM_str_hash_bind_nt(tc, dest_hashtable, &new_entry->hash_handle);
        MVM_gc_write_barrier(tc, &(dest_root->header), &(key->common.header));
        iterator = MVM_str_hash_next(tc, src_hashtable, iterator);
    }
}

/* Adds held objects to the GC worklist. */
static void MVMHash_gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashBody     *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVM_gc_worklist_presize_for(tc, worklist, 2 * MVM_str_hash_count(hashtable));
    if (worklist->include_gen2) {
        MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
        MVMHashEntry *current;
        while ((current = MVM_str_hash_current(tc, hashtable, iterator))) {
            MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &current->hash_handle.key);
            MVM_gc_worklist_add_include_gen2_nocheck(tc, worklist, &current->value);
            iterator = MVM_str_hash_next(tc, hashtable, iterator);
        }
    }
    else {
        MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
        MVMHashEntry *current;
        while ((current = MVM_str_hash_current(tc, hashtable, iterator))) {
            MVM_gc_worklist_add_no_include_gen2_nocheck(tc, worklist, &current->hash_handle.key);
            MVM_gc_worklist_add_object_no_include_gen2_nocheck(tc, worklist, &current->value);
            iterator = MVM_str_hash_next(tc, hashtable, iterator);
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHash *h = (MVMHash *)obj;
    MVMStrHashTable *hashtable = &(h->body.hashtable);

    MVM_str_hash_demolish(tc, hashtable);
}

static void at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister *result, MVMuint16 kind) {
    MVMHashBody   *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");

    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, (MVMString *)key_obj);
    result->o = entry != NULL ? entry->value : tc->instance->VMNull;
}
void MVMHash_at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister *result, MVMuint16 kind) {
    at_key(tc, st, root, data, key_obj, result, kind);
}

static void bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister value, MVMuint16 kind) {
    MVMHashBody   *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

    MVMString *key = (MVMString *)key_obj;
    if (!MVM_str_hash_key_is_valid(tc, key)) {
        MVM_str_hash_key_throw_invalid(tc, key);
    }
    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "MVMHash representation does not support native type storage");

    if (!MVM_str_hash_entry_size(hashtable)) {
        MVM_str_hash_build(tc, hashtable, sizeof(MVMHashEntry));
    }

    MVMHashEntry *entry = MVM_str_hash_lvalue_fetch_nt(tc, hashtable, key);
    MVM_ASSIGN_REF(tc, &(root->header), entry->value, value.o);
    if (!entry->hash_handle.key) {
        entry->hash_handle.key = key;
        MVM_gc_write_barrier(tc, &(root->header), &(key->common.header));
    }
}
void MVMHash_bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj, MVMRegister value, MVMuint16 kind) {
    bind_key(tc, st, root, data, key_obj, value, kind);
}
static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;
    return MVM_str_hash_count(&(body->hashtable));}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj) {
    MVMHashBody   *body = (MVMHashBody *)data;
    /* key_obj checked in MVM_str_hash_fetch */
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, (MVMString *)key_obj);
    return entry != NULL;
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key_obj) {
    MVMHashBody *body = (MVMHashBody *)data;
    MVMString *key = get_string_key(tc, key_obj);
    MVMStrHashTable *hashtable = &(body->hashtable);

    MVM_str_hash_delete(tc, hashtable, key);
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
    MVMStrHashTable *hashtable = &(body->hashtable);
    if (MVM_str_hash_entry_size(hashtable)) {
        // XXX Is this a valid assumption?
        MVM_oops(tc, "deserialize on MVMHash that is already initialized");
    }
    MVM_str_hash_build(tc, hashtable, sizeof(MVMHashEntry));
    MVMint64 elems = MVM_serialization_read_int(tc, reader);
    MVMint64 i;
    for (i = 0; i < elems; i++) {
        MVMString *key = MVM_serialization_read_str(tc, reader);
        if (!MVM_str_hash_key_is_valid(tc, key)) {
            MVM_str_hash_key_throw_invalid(tc, key);
        }
        MVMObject *value = MVM_serialization_read_ref(tc, reader);
        MVMHashEntry *entry = MVM_fixed_size_alloc(tc, tc->instance->fsa,
            sizeof(MVMHashEntry));
        entry->hash_handle.key = key;
        MVM_ASSIGN_REF(tc, &(root->header), entry->value, value);
        MVM_str_hash_bind_nt(tc, hashtable, &entry->hash_handle);
    }
}

/* Serialize the representation. */
static MVMThreadContext *cmp_tc;
static int cmp_strings(const void *s1, const void *s2) {
    return MVM_string_compare(cmp_tc, *(MVMString **)s1, *(MVMString **)s2);
}
static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMHashBody *body = (MVMHashBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMuint64 elems = MVM_str_hash_count(hashtable);
    MVMString **keys = MVM_malloc(sizeof(MVMString *) * elems);
    MVMuint64 i = 0;
    MVM_serialization_write_int(tc, writer, elems);
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
    MVMHashEntry *current;
    while ((current = MVM_str_hash_current(tc, hashtable, iterator))) {
        keys[i++] = current->hash_handle.key;
        iterator = MVM_str_hash_next(tc, hashtable, iterator);
    }
    cmp_tc = tc;
    qsort(keys, elems, sizeof(MVMString*), cmp_strings);
    for (i = 0; i < elems; i++) {
        MVMHashEntry *entry = MVM_str_hash_fetch_nt(tc, hashtable, keys[i]);
        MVM_serialization_write_str(tc, writer, keys[i]);
        MVM_serialization_write_ref(tc, writer, entry->value);
    }
    MVM_free(keys);
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMHash);
}

/* Bytecode specialization for this REPR. */
static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
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
}

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;

    return sizeof(MVMHashEntry) * MVM_str_hash_count(&(body->hashtable));
}

/* Initializes the representation. */
const MVMREPROps * MVMHash_initialize(MVMThreadContext *tc) {
    return &MVMHash_this_repr;
}

static const MVMREPROps MVMHash_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    {
        at_key,
        bind_key,
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
