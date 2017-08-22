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
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    /* NOTE: if we really wanted to, we could avoid rehashing... */
    HASH_ITER(hash_handle, src_body->hash_head, current, tmp, bucket_tmp) {
        MVMHashEntry *new_entry = MVM_malloc(sizeof(MVMHashEntry));
        MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->value, current->value);
        MVM_HASH_BIND(tc, dest_body->hash_head, MVM_HASH_KEY(current), new_entry);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMHashEntry *current, *tmp;
    unsigned bucket_tmp;

    HASH_ITER(hash_handle, body->hash_head, current, tmp, bucket_tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->hash_handle.key);
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
    if (kind == MVM_reg_obj) {
        MVMHashEntry *entry;
        MVM_HASH_GET(tc, body->hash_head, name, entry);
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
    if (kind == MVM_reg_obj) {
        MVMHashEntry *entry;
        MVM_HASH_GET(tc, body->hash_head, name, entry);
        if (!entry) {
            entry = MVM_malloc(sizeof(MVMHashEntry));
            MVM_ASSIGN_REF(tc, &(root->header), entry->value, value_reg.o);
            MVM_HASH_BIND(tc, body->hash_head, name, entry);
            MVM_gc_write_barrier(tc, &(root->header), &(name->common.header));
        }
        else {
            MVM_ASSIGN_REF(tc, &(root->header), entry->value, value_reg.o);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");
    }
}

static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMHashAttrStoreBody *body = (MVMHashAttrStoreBody *)data;
    MVMHashEntry *entry;
    MVM_HASH_GET(tc, body->hash_head, name, entry);
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
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
