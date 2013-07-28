#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(HashAttrStore);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;

    /* this must be initialized to NULL */
    body->hash_head = NULL;
}

static void extract_key(MVMThreadContext *tc, void **kdata, size_t *klen, MVMObject *key) {
    MVM_HASH_EXTRACT_KEY(tc, kdata, klen, key, "HashAttrStore representation requires MVMString keys")
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    HashAttrStoreBody *src_body  = (HashAttrStoreBody *)src;
    HashAttrStoreBody *dest_body = (HashAttrStoreBody *)dest;
    MVMHashEntry *current, *tmp;

    /* NOTE: if we really wanted to, we could avoid rehashing... */
    HASH_ITER(hash_handle, src_body->hash_head, current, tmp) {
        size_t klen;
        void *kdata;
        MVMHashEntry *new_entry = malloc(sizeof(MVMHashEntry));
        new_entry->key = current->key;
        new_entry->value = current->value;
        extract_key(tc, &kdata, &klen, current->key);

        HASH_ADD_KEYPTR(hash_handle, dest_body->hash_head, kdata, klen, new_entry);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    MVMHashEntry *current, *tmp;

    HASH_ITER(hash_handle, body->hash_head, current, tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->key);
        MVM_gc_worklist_add(tc, worklist, &current->value);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    HashAttrStore *h = (HashAttrStore *)obj;
    MVMHashEntry *current, *tmp;

    /* The macros already check for null. Also, must not delete the head
     * node until after calling clear, or we look into freed memory. */
    HASH_ITER(hash_handle, h->body.hash_head, current, tmp) {
        if (current != h->body.hash_head)
            free(current);
    }
    HASH_CLEAR(hash_handle, h->body.hash_head);
    if (h->body.hash_head)
        free(h->body.hash_head);
}

static void get_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister *result_reg, MVMuint16 kind) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    if (kind == MVM_reg_obj) {
        extract_key(tc, &kdata, &klen, (MVMObject *)name);
        HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
        result_reg->o = entry != NULL ? entry->value : NULL;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");
    }
}

static void bind_attribute(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMRegister value_reg, MVMuint16 kind) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    if (kind == MVM_reg_obj) {
        extract_key(tc, &kdata, &klen, (MVMObject *)name);

        /* first check whether we must update the old entry. */
        HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
        if (!entry) {
            entry = malloc(sizeof(MVMHashEntry));
            HASH_ADD_KEYPTR(hash_handle, body->hash_head, kdata, klen, entry);
        }
        else
            entry->hash_handle.key = (void *)kdata;
        entry->key = (MVMObject *)name;
        entry->value = value_reg.o;
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation does not support native attribute storage");
    }
}

static MVMint64 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
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

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(HashAttrStore);
}

/* Initializes the representation. */
MVMREPROps * HashAttrStore_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_mark = gc_mark;
    this_repr->gc_free = gc_free;
    this_repr->get_storage_spec = get_storage_spec;
    this_repr->attr_funcs = malloc(sizeof(MVMREPROps_Attribute));
    this_repr->attr_funcs->get_attribute = get_attribute;
    this_repr->attr_funcs->bind_attribute = bind_attribute;
    this_repr->attr_funcs->is_attribute_initialized = is_attribute_initialized;
    this_repr->attr_funcs->hint_for = hint_for;
    this_repr->compose = compose;
    this_repr->deserialize_stable_size = deserialize_stable_size;
    return this_repr;
}
