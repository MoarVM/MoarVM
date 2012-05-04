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
    return MVM_gc_allocate_object(tc, st, sizeof(MVMKnowHOWREPR));
}

/* Initialize a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMObject *methods, *attributes;
    MVMObject *BOOTArray = tc->instance->boot_types->BOOTArray;
    MVMObject *BOOTHash  = tc->instance->boot_types->BOOTHash;
    MVMKnowHOWREPRBody *body = (MVMKnowHOWREPRBody *)data;
    
    methods = REPR(BOOTHash)->allocate(tc, STABLE(BOOTHash));
    MVM_WB(tc, root, methods);
    body->methods = methods;
    
    attributes = REPR(BOOTArray)->allocate(tc, STABLE(BOOTArray));
    MVM_WB(tc, root, attributes);
    body->attributes = attributes;
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMKnowHOWREPRBody *src_body  = (MVMKnowHOWREPRBody *)src;
    MVMKnowHOWREPRBody *dest_body = (MVMKnowHOWREPRBody *)dest;
    MVM_WB(tc, dest_root, src_body->methods);
    MVM_WB(tc, dest_root, src_body->attributes);
    MVM_WB(tc, dest_root, src_body->name);
    dest_body->methods    = src_body->methods;
    dest_body->attributes = src_body->attributes;
    dest_body->name       = src_body->name;
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
MVMREPROps * MVMKnowHOWREPR_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. Note
     * that to support the bootstrap, this one REPR guards against a
     * duplicate initialization (which we actually will do). */
    if (!this_repr) {
        this_repr = malloc(sizeof(MVMREPROps));
        memset(this_repr, 0, sizeof(MVMREPROps));
        this_repr->type_object_for = type_object_for;
        this_repr->allocate = allocate;
        this_repr->initialize = initialize;
        this_repr->copy_to = copy_to;
        this_repr->get_storage_spec = get_storage_spec;
    }
    return this_repr;
}
