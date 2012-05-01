#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st, sizeof(MVMHash));
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMHashBody *body = (MVMHashBody *)data;
    apr_pool_create(&body->pool, NULL);
    body->hash = apr_hash_make(body->pool);
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMHashBody *src_body  = (MVMHashBody *)src;
    MVMHashBody *dest_body = (MVMHashBody *)dest;
    apr_hash_index_t *idx;
    
    /* Create a new pool for the target hash, and the hash itself. */
    apr_pool_create(&dest_body->pool, NULL);
    dest_body->hash = apr_hash_make(dest_body->pool);
 
    /* Copy; note don't use the APR copy function for this as we
     * want to write barrier (which may be overkill as the target
     * is presumably first generation...but not so fun to debug
     * the upshot if that turns out not to be true somehow). */
    for (idx = apr_hash_first(dest_body->pool, src_body->hash); idx; idx = apr_hash_next(idx)) {
        void *key, *val;
        apr_ssize_t klen;
        apr_hash_this(idx, &key, &klen, &val);
        MVM_WB(tc, dest_root, val);
        apr_hash_set(dest_body->hash, key, klen, val);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMHash *h = (MVMHash *)obj;
    apr_pool_destroy(h->body.pool);
    h->body.pool = NULL;
    h->body.hash = NULL;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
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
    this_repr->gc_free = gc_free;
    this_repr->get_storage_spec = get_storage_spec;
    return this_repr;
}
