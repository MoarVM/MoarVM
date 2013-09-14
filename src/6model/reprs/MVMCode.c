#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    if (IS_CONCRETE(invokee)) {
        MVMCode *code = (MVMCode *)invokee;
        MVM_frame_invoke(tc, code->body.sf, callsite, args, code->body.outer, invokee);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot invoke code type object");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMCode);
    });

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
    MVM_ASSIGN_REF(tc, dest_root, dest_body->sf, src_body->sf);
    if (src_body->outer) {
        dest_body->outer = MVM_frame_inc_ref(tc, src_body->outer);
    }
    else {
        /* XXX Hack (prior_invocation is a hack) for the sake of rules; can go
         * away after the cleanup that removes prior_invocation. */
        MVMStaticFrame *so = src_body->sf->body.outer;
        if (so && so->body.prior_invocation)
            dest_body->outer = MVM_frame_inc_ref(tc, so->body.prior_invocation);
    }
    /* XXX should these next two really be copied? */
    dest_body->is_static = dest_body->is_static;
    dest_body->is_compiler_stub = dest_body->is_compiler_stub;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->code_object, src_body->code_object);
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMCodeBody *body = (MVMCodeBody *)data;
    MVM_gc_worklist_add_frame(tc, worklist, body->outer);
    MVM_gc_worklist_add(tc, worklist, &body->code_object);
    MVM_gc_worklist_add(tc, worklist, &body->sf);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMCode *code_obj = (MVMCode *)obj;
    if (code_obj->body.outer) {
        code_obj->body.outer = MVM_frame_dec_ref(tc, code_obj->body.outer);
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

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
MVMREPROps * MVMCode_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    &MVM_REPR_DEFAULT_ATTR_FUNCS,
    &MVM_REPR_DEFAULT_BOX_FUNCS,
    &MVM_REPR_DEFAULT_POS_FUNCS,
    &MVM_REPR_DEFAULT_ASS_FUNCS,
    NULL, /* elems */
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "MVMCode", /* name */
    0,  /* ID */
    1, /* refs_frames */
};