#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps HashAttrStore_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &HashAttrStore_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMHashAttrStore);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashAttrStoreBody *src_body  = (MVMHashAttrStoreBody *)src;
    MVMHashAttrStoreBody *dest_body = (MVMHashAttrStoreBody *)dest;

    MVMStrHashTable *src_hashtable = &(src_body->hashtable);
    MVMStrHashTable *dest_hashtable = &(dest_body->hashtable);
    if (MVM_str_hash_entry_size(tc, dest_hashtable)) {
        /* copy_to is, on reference types, only ever used as part of clone, and
         * that will always target a freshly created object.
         * So this should be unreachable. */
        MVM_oops(tc, "copy_to on MVMHash that is already initialized");
    }
    MVM_str_hash_build(tc, dest_hashtable, sizeof(MVMHashEntry),
                       MVM_str_hash_count(tc, src_hashtable));
    MVMStrHashIterator iterator = MVM_str_hash_first(tc, src_hashtable);
    while (!MVM_str_hash_at_end(tc, src_hashtable, iterator)) {
        MVMHashEntry *entry = MVM_str_hash_current_nocheck(tc, src_hashtable, iterator);
        MVMHashEntry *new_entry = MVM_str_hash_insert_nocheck(tc, dest_hashtable, entry->hash_handle.key);
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->value, entry->value);
        MVM_gc_write_barrier(tc, &(dest_root->header), &(new_entry->hash_handle.key->common.header));
        iterator = MVM_str_hash_next_nocheck(tc, src_hashtable, iterator);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVM_gc_worklist_presize_for(tc, worklist, 2 * MVM_str_hash_count(tc, hashtable));

    MVMStrHashIterator iterator = MVM_str_hash_first(tc, hashtable);
    while (!MVM_str_hash_at_end(tc, hashtable, iterator)) {
        MVMHashEntry *current = MVM_str_hash_current_nocheck(tc, hashtable, iterator);
        MVM_gc_worklist_add(tc, worklist, &current->hash_handle.key);
        MVM_gc_worklist_add(tc, worklist, &current->value);
        iterator = MVM_str_hash_next_nocheck(tc, hashtable, iterator);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHashAttrStore *h = (MVMHashAttrStore *)obj;
    MVMStrHashTable *hashtable = &(h->body.hashtable);

    MVM_str_hash_demolish(tc, hashtable);
}

static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");

    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, name);
    result_reg->o = entry != NULL ? entry->value : tc->instance->VMNull;
}

static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);

    if (!MVM_str_hash_key_is_valid(tc, name)) {
        MVM_str_hash_key_throw_invalid(tc, name);
    }
    if (MVM_UNLIKELY(kind != MVM_reg_obj))
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");

    if (!MVM_str_hash_entry_size(tc, hashtable)) {
        MVM_str_hash_build(tc, hashtable, sizeof(MVMHashEntry), 0);
    }

    MVMHashEntry *entry = MVM_str_hash_lvalue_fetch_nocheck(tc, hashtable, name);
    MVM_ASSIGN_REF(tc, &(root->header), entry->value, value_reg.o);
    if (!entry->hash_handle.key) {
        entry->hash_handle.key = name;
        MVM_gc_write_barrier(tc, &(root->header), &(name->common.header));
    }
}

static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMStrHashTable *hashtable = &(body->hashtable);
    MVMHashEntry *entry = MVM_str_hash_fetch(tc, hashtable, name);
    return entry != NULL;
}

static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_handle, MVMString *name) {
    return MVM_NO_HINT;
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
    st->size = sizeof(MVMHashAttrStore);
}

static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;

    return MVM_str_hash_allocated_size(tc, &(body->hashtable));
}

/* Initializes the representation. */
const MVMREPROps * MVMHashAttrStore_initialize(MVMThreadContext *tc) {
    return &HashAttrStore_this_repr;
}

static const MVMREPROps HashAttrStore_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    {
        get_attribute,
        bind_attribute,
        hint_for,
        is_attribute_initialized,
        MVM_REPR_DEFAULT_ATTRIBUTE_AS_ATOMIC
    },   /* attr_funcs */
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
    "HashAttrStore", /* name */
    MVM_REPR_ID_HashAttrStore,
    unmanaged_size,
    NULL, /* describe_refs */
};
