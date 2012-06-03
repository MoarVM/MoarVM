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
    return MVM_gc_allocate_object(tc, st, sizeof(MVMArray));
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMArrayBody *src_body  = (MVMArrayBody *)src;
    MVMArrayBody *dest_body = (MVMArrayBody *)dest;
    dest_body->elems = src_body->elems;
    dest_body->alloc  = src_body->alloc;
    dest_body->data   = malloc(sizeof(MVMObject *) * dest_body->alloc);
    memcpy(dest_body->data, src_body->data, sizeof(MVMObject *) * dest_body->elems);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMArray *arr = (MVMArray *)obj;
    free(arr->body.data);
    arr->body.data = NULL;
    arr->body.elems = arr->body.alloc = 0;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

static void expand_to_at_least(MVMArrayBody *body, MVMuint64 min_elems) {
    MVMuint64 alloc = body->alloc + 4 > min_elems ? body->alloc + 4 : min_elems;
    if (body->alloc == 0)
        body->data = malloc(sizeof(MVMObject *) * alloc);
    else
        body->data = realloc(body->data, sizeof(MVMObject *) * alloc);
    body->alloc = alloc;
}

static void * at_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation does not support native type storage");
}

static MVMObject * at_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return index < body->elems ? body->data[index] : tc->instance->null;
}

static void bind_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation does not support native type storage");
}

static void bind_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index, MVMObject *obj) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    if (index >= body->alloc)
        expand_to_at_least(body, index + 1);
    if (index >= body->elems)
        index = body->elems + 1;
    MVM_WB(tc, root, obj);
    body->data[index] = obj;
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return body->elems;
}

static void preallocate(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    expand_to_at_least(body, count);
}

static void trim_to(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    if (count > body->elems)
        MVM_exception_throw_adhoc(tc,
            "Trimming an array should not increase its number of elements");
    body->elems = count;
}

static void make_hole(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    MVMuint64 new_elems = body->elems + count;
    if (new_elems > body->alloc)
        expand_to_at_least(body, new_elems);
    MVM_exception_throw_adhoc(tc,
        "MVMArray does not yet implement make_hole");
}

static void delete_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    MVM_exception_throw_adhoc(tc,
        "MVMArray does not yet implement delete_elems");
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Initializes the representation. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_free = gc_free;
    this_repr->get_storage_spec = get_storage_spec; 
    this_repr->pos_funcs = malloc(sizeof(MVMREPROps_Positional));
    this_repr->pos_funcs->at_pos_ref = at_pos_ref;
    this_repr->pos_funcs->at_pos_boxed = at_pos_boxed;
    this_repr->pos_funcs->bind_pos_ref = bind_pos_ref;
    this_repr->pos_funcs->bind_pos_boxed = bind_pos_boxed;
    this_repr->pos_funcs->elems = elems;
    this_repr->pos_funcs->preallocate = preallocate;
    this_repr->pos_funcs->trim_to = trim_to;
    this_repr->pos_funcs->make_hole = make_hole;
    this_repr->pos_funcs->delete_elems = delete_elems;
    this_repr->pos_funcs->get_elem_storage_spec = get_elem_storage_spec;
    return this_repr;
}
