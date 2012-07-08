#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    if (IS_CONCRETE(invokee)) {
        MVMCode *code = (MVMCode *)invokee;
        MVM_frame_invoke(tc, code->body.sf, callsite, args, code->body.outer);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot invoke code type object");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;
    
    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    
    obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    st->invoke = invoke_handler;
    st->size = sizeof(MVMCode);
    
    MVM_gc_root_temp_pop(tc);
    
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCodeBody *src_body  = (MVMCodeBody *)src;
    MVMCodeBody *dest_body = (MVMCodeBody *)dest;
    MVM_panic(MVM_exitcode_NYI, "MVMCode copy_to NYI");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCodeBody *body = (MVMCodeBody *)data;
    if (body->outer)
        MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, body->outer);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCode *code_obj = (MVMCode *)obj;
    if (code_obj->body.outer) {
        MVM_frame_dec_ref(tc, code_obj->body.outer);
        code_obj->body.outer = NULL;
    }
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    /* XXX in the end we'll support inlining of this... */
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Initializes the representation. */
MVMREPROps * MVMCode_initialize(MVMThreadContext *tc) {
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
    return this_repr;
}
