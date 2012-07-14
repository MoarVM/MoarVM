#include "moarvm.h"

/* We naughtily break the APR encapsulation here. Why? Because we need to
 * get at the address where a hash key/value is stored, for GC purposes. */
struct apr_hash_entry_t {
    struct apr_hash_entry_t *next;
    unsigned int             hash;
    const void              *key;
    apr_ssize_t              klen;
    const void              *val;
};
struct apr_hash_index_t {
    apr_hash_t                *ht;
    struct apr_hash_entry_t   *this, *next;
    unsigned int               index;
};

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    st->size = sizeof(HashAttrStore);
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    apr_status_t rv;
    
    if ((rv = apr_pool_create(&body->pool, NULL)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to initialize HashAttrStoreBody: ");
    }
    
    body->key_hash = apr_hash_make(body->pool);
    body->value_hash = apr_hash_make(body->pool);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    HashAttrStoreBody *src_body  = (HashAttrStoreBody *)src;
    HashAttrStoreBody *dest_body = (HashAttrStoreBody *)dest;
    apr_hash_index_t *idx;
    
    /* Create a new pool for the target hash, and the hash itself. */
    apr_status_t rv;
    
    if ((rv = apr_pool_create(&dest_body->pool, NULL)) != APR_SUCCESS) {
        MVM_exception_throw_apr_error(tc, rv, "Failed to copy HashAttrStoreBody: ");
    }
    
    dest_body->key_hash = apr_hash_make(dest_body->pool);
    dest_body->value_hash = apr_hash_make(dest_body->pool);
 
    /* Copy; note don't use the APR copy function for this as we
     * want to write barrier (which may be overkill as the target
     * is presumably first generation...but not so fun to debug
     * the upshot if that turns out not to be true somehow). */
    for (idx = apr_hash_first(dest_body->pool, src_body->key_hash); idx; idx = apr_hash_next(idx)) {
        const void *key;
        void *val;
        apr_ssize_t klen;
        apr_hash_this(idx, &key, &klen, &val);
        /* XXX Needs a write barrier, but APR. */
        apr_hash_set(dest_body->key_hash, key, klen, val);
    }
    for (idx = apr_hash_first(dest_body->pool, src_body->value_hash); idx; idx = apr_hash_next(idx)) {
        const void *key;
        void *val;
        apr_ssize_t klen;
        apr_hash_this(idx, &key, &klen, &val);
        /* Needs a write barrier, but APR. */
        apr_hash_set(dest_body->value_hash, key, klen, val);
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    apr_hash_index_t *hi;
    for (hi = apr_hash_first(NULL, body->key_hash); hi; hi = apr_hash_next(hi)) {
        struct apr_hash_entry_t *this = hi->this;
        MVM_gc_worklist_add(tc, worklist, &this->val);
    }
    for (hi = apr_hash_first(NULL, body->value_hash); hi; hi = apr_hash_next(hi)) {
        struct apr_hash_entry_t *this = hi->this;
        MVM_gc_worklist_add(tc, worklist, &this->val);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    HashAttrStore *h = (HashAttrStore *)obj;
    apr_pool_destroy(h->body.pool);
    h->body.pool = NULL;
    h->body.key_hash = NULL;
    h->body.value_hash = NULL;
}

static void extract_key(MVMThreadContext *tc, void **kdata, apr_ssize_t *klen, MVMObject *key) {
    if (REPR(key)->ID == MVM_REPR_ID_MVMString && IS_CONCRETE(key)) {
        *kdata = ((MVMString *)key)->body.data;
        *klen  = ((MVMString *)key)->body.graphs * sizeof(MVMint32);
    }
    else {
        MVM_exception_throw_adhoc(tc,
            "HashAttrStore representation requires MVMString keys");
    }
}

static MVMObject * get_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    void *kdata, *value;
    apr_ssize_t klen;
    extract_key(tc, &kdata, &klen, (MVMObject *)name);
    value = apr_hash_get(body->value_hash, kdata, klen);
    return value ? (MVMObject *)value : NULL;
}

static void * get_attribute_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVM_exception_throw_adhoc(tc,
        "HashAttrStore representation does not support native attribute storage");
}

static void bind_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMObject *value) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    void *kdata;
    apr_ssize_t klen;
    extract_key(tc, &kdata, &klen, (MVMObject *)name);
    apr_hash_set(body->key_hash, kdata, klen, (MVMObject *)name);
    apr_hash_set(body->value_hash, kdata, klen, value);
}

static void bind_attribute_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, void *value) {
    MVM_exception_throw_adhoc(tc,
        "HashAttrStore representation does not support native attribute storage");
}

static MVMint32 is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    HashAttrStoreBody *body = (HashAttrStoreBody *)data;
    void *kdata;
    apr_ssize_t klen;
    extract_key(tc, &kdata, &klen, (MVMObject *)name);
    return apr_hash_get(body->value_hash, kdata, klen) != NULL;
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
    this_repr->attr_funcs->get_attribute_boxed = get_attribute_boxed;
    this_repr->attr_funcs->get_attribute_ref = get_attribute_ref;
    this_repr->attr_funcs->bind_attribute_boxed = bind_attribute_boxed;
    this_repr->attr_funcs->bind_attribute_ref = bind_attribute_ref;
    this_repr->attr_funcs->is_attribute_initialized = is_attribute_initialized;
    this_repr->attr_funcs->hint_for = hint_for;
    this_repr->compose = compose;
    return this_repr;
}
