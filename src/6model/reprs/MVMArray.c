#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;
    
    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    
    obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    st->size = sizeof(MVMArray);
    
    MVM_gc_root_temp_pop(tc);
    
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
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

static void * at_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation does not support native type storage");
}

static MVMObject * at_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return index < body->elems ? body->data[index] : NULL;
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
        body->elems = index + 1;
    MVM_ASSIGN_REF(tc, root, body->data[index], obj);
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    return body->elems;
}

static void set_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void push_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void push_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *obj) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void * pop_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMObject * pop_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void unshift_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void unshift_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *obj) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void * shift_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, void *target) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMObject * shift_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static void splice(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *target_array, MVMuint64 offset, MVMuint64 elems) {
    MVM_exception_throw_adhoc(tc,
        "MVMArray representation not fully implemented yet");
}

static MVMStorageSpec get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMArrayBody *body = (MVMArrayBody *)data;
    MVMuint64 elems, i;
    elems = body->elems;
    for (i = 0; i < elems; i++)
        MVM_gc_worklist_add(tc, worklist, &body->data[i]);
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
    this_repr->pos_funcs->set_elems = set_elems;
    this_repr->pos_funcs->push_ref = push_ref;
    this_repr->pos_funcs->push_boxed = push_boxed;
    this_repr->pos_funcs->pop_ref = pop_ref;
    this_repr->pos_funcs->pop_boxed = pop_boxed;
    this_repr->pos_funcs->unshift_ref = unshift_ref;
    this_repr->pos_funcs->unshift_boxed = unshift_boxed;
    this_repr->pos_funcs->shift_ref = shift_ref;
    this_repr->pos_funcs->shift_boxed = shift_boxed;
    this_repr->pos_funcs->splice = splice;
    this_repr->pos_funcs->get_elem_storage_spec = get_elem_storage_spec;
    this_repr->gc_mark = gc_mark;
    return this_repr;
}
