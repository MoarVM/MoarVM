#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    if (IS_CONCRETE(invokee)) {
        MVMLexotic *lex = (MVMLexotic *)invokee;

        /* Get argument and set as result. */
        MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
        MVM_args_proc_init(tc, &arg_ctx, callsite, args);
        MVM_ASSIGN_REF(tc, invokee, lex->body.result,
            MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED).arg.o);
        MVM_args_proc_cleanup(tc, &arg_ctx);

        /* Unwind to the lexotic handler. */
        MVM_exception_gotolexotic(tc, lex->body.handler, lex->body.frame);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot invoke Lexotic type object");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMLexotic);
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
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation Lexotic");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMLexoticBody *lb = (MVMLexoticBody *)data;
    MVM_gc_worklist_add(tc, worklist, &lb->result);
    if (lb->frame)
        MVM_gc_root_add_frame_roots_to_worklist(tc, worklist, lb->frame);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMLexotic *lex = (MVMLexotic *)obj;
    if (lex->body.frame) {
        MVM_frame_dec_ref(tc, lex->body.frame);
        lex->body.frame = NULL;
    }
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
MVMREPROps * MVMLexotic_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    if (!this_repr) {
        this_repr = malloc(sizeof(MVMREPROps));
        memset(this_repr, 0, sizeof(MVMREPROps));
        this_repr->refs_frames = 1;
        this_repr->type_object_for = type_object_for;
        this_repr->allocate = allocate;
        this_repr->initialize = initialize;
        this_repr->copy_to = copy_to;
        this_repr->gc_mark = gc_mark;
        this_repr->gc_free = gc_free;
        this_repr->get_storage_spec = get_storage_spec;
        this_repr->compose = compose;
    }
    return this_repr;
}
