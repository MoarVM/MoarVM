#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMHashAttrStore);
    });

    return st->WHAT;
}

static void extract_key(MVMThreadContext *tc, void **kdata, size_t *klen, MVMObject *key) {
    MVM_HASH_EXTRACT_KEY(tc, kdata, klen, key, "HashAttrStore representation requires MVMString keys")
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashAttrStoreBody *src_body  = (MVMHashAttrStoreBody *)src;
    MVMHashAttrStoreBody *dest_body = (MVMHashAttrStoreBody *)dest;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    /* NOTE: if we really wanted to, we could avoid rehashing... */
    HASH_ITER(hash_handle, src_body->hash_head, current, tmp, bucket_tmp) {
        size_t klen;
        void *kdata;
        MVMHashEntry *new_entry = MVM_malloc(sizeof(MVMHashEntry));
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->key, current->key);
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->value, current->value);
        extract_key(tc, &kdata, &klen, new_entry->key);

        HASH_ADD_KEYPTR(hash_handle, dest_body->hash_head, kdata, klen, new_entry);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    HASH_ITER(hash_handle, body->hash_head, current, tmp, bucket_tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->key);
        MVM_gc_worklist_add(tc, worklist, &current->value);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHashAttrStore *h = (MVMHashAttrStore *)obj;
    MVM_HASH_DESTROY(hash_handle, MVMHashEntry, h->body.hash_head);
}

static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    if (kind == MVM_reg_obj) {
        extract_key(tc, &kdata, &klen, (MVMObject *)name);
        HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
        result_reg->o = entry != NULL ? entry->value : tc->instance->VMNull;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");
    }
}

static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    if (kind == MVM_reg_obj) {
        extract_key(tc, &kdata, &klen, (MVMObject *)name);

        /* first check whether we must update the old entry. */
        HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
        if (!entry) {
            entry = MVM_malloc(sizeof(MVMHashEntry));
            HASH_ADD_KEYPTR(hash_handle, body->hash_head, kdata, klen, entry);
        }
        else
            entry->hash_handle.key = (void *)kdata;
        MVM_ASSIGN_REF(tc, &(root->header), entry->key, (MVMObject *)name);
        MVM_ASSIGN_REF(tc, &(root->header), entry->value, value_reg.o);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");
    }
}

static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;

    extract_key(tc, &kdata, &klen, (MVMObject *)name);
    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
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

/* Initializes the representation. */
const MVMREPROps * MVMHashAttrStore_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    {
        get_attribute,
        bind_attribute,
        hint_for,
        is_attribute_initialized
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
    0, /* refs_frames */
};
