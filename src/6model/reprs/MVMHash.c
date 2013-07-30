#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMHash);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;

    /* this must be initialized to NULL */
    body->hash_head = NULL;
}

static void extract_key(MVMThreadContext *tc, void **kdata, size_t *klen, MVMObject *key) {
    MVM_HASH_EXTRACT_KEY(tc, kdata, klen, key, "MVMHash representation requires MVMString keys")
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashBody *src_body  = (MVMHashBody *)src;
    MVMHashBody *dest_body = (MVMHashBody *)dest;
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
    MVMHashBody *body = (MVMHashBody *)data;
    MVMHashEntry *current, *tmp;

    HASH_ITER(hash_handle, body->hash_head, current, tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->key);
        MVM_gc_worklist_add(tc, worklist, &current->value);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHash *h = (MVMHash *)obj;
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

static void * at_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVM_exception_throw_adhoc(tc,
        "MVMHash representation does not support native type storage");
}

static MVMObject * at_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    extract_key(tc, &kdata, &klen, key);
    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    return entry != NULL ? entry->value : NULL;
}

static void bind_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, void *value_addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMHash representation does not support native type storage");
}

static void bind_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMObject *value) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;

    extract_key(tc, &kdata, &klen, key);

    /* first check whether we can must update the old entry. */
    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    if (!entry) {
        entry = malloc(sizeof(MVMHashEntry));
        HASH_ADD_KEYPTR(hash_handle, body->hash_head, kdata, klen, entry);
    }
    else
        entry->hash_handle.key = (void *)kdata;
    entry->key = key;
    entry->value = value;
    MVM_WB(tc, root, key);
    MVM_WB(tc, root, value);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;
    return HASH_CNT(hash_handle, body->hash_head);
}

static MVMuint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMHashBody *body = (MVMHashBody *)data;
    void *kdata;
    MVMHashEntry *entry;
    size_t klen;
    extract_key(tc, &kdata, &klen, key);

    HASH_FIND(hash_handle, body->hash_head, kdata, klen, entry);
    return entry != NULL;
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMHashBody *body = (MVMHashBody *)data;
    MVMHashEntry *old_entry;
    size_t klen;
    void *kdata;
    extract_key(tc, &kdata, &klen, key);

    HASH_FIND(hash_handle, body->hash_head, kdata, klen, old_entry);
    if (old_entry) {
        HASH_DELETE(hash_handle, body->hash_head, old_entry);
        free(old_entry);
    }
}

static MVMStorageSpec get_value_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
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
    /* XXX key and value types will be communicated here */
}

/* Set the size of the STable. */
static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMHash);
}

/* Initializes the representation. */
MVMREPROps * MVMHash_initialize(MVMThreadContext *tc) {
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
    this_repr->ass_funcs = malloc(sizeof(MVMREPROps_Associative));
    this_repr->ass_funcs->at_key_ref = at_key_ref;
    this_repr->ass_funcs->at_key_boxed = at_key_boxed;
    this_repr->ass_funcs->bind_key_ref = bind_key_ref;
    this_repr->ass_funcs->bind_key_boxed = bind_key_boxed;
    this_repr->ass_funcs->exists_key = exists_key;
    this_repr->ass_funcs->delete_key = delete_key;
    this_repr->ass_funcs->get_value_storage_spec = get_value_storage_spec;
    this_repr->compose = compose;
    this_repr->elems = elems;
    this_repr->deserialize_stable_size = deserialize_stable_size;
    return this_repr;
}
