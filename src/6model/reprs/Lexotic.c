#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    if (IS_CONCRETE(invokee)) {
        /* Get argument and set as result. Need to root invokee, as argument
         * processing may box. */
        MVMROOT(tc, invokee, {
            MVMObject *result;
            MVMArgProcContext arg_ctx; arg_ctx.named_used = NULL;
            MVM_args_proc_init(tc, &arg_ctx, callsite, args);
            result = MVM_args_get_pos_obj(tc, &arg_ctx, 0, MVM_ARG_REQUIRED).arg.o;
            MVM_ASSIGN_REF(tc, &(invokee->header), ((MVMLexotic *)invokee)->body.result, result);
            MVM_args_proc_cleanup(tc, &arg_ctx);
        });

        /* Unwind to the lexotic handler. */
        {
            MVMLexotic *lex = (MVMLexotic *)invokee;
            MVM_exception_gotolexotic(tc, lex->body.handler_idx, lex->body.sf);
        }
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot invoke Lexotic type object");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMLexotic);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_exception_throw_adhoc(tc, "Cannot copy object with representation Lexotic");
}

/* Called by the VM to mark any GCable items. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMLexoticBody *lb = (MVMLexoticBody *)data;
    MVM_gc_worklist_add(tc, worklist, &lb->sf);
    MVM_gc_worklist_add(tc, worklist, &lb->result);
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMLexoticBody *lb = (MVMLexoticBody *)data;
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss, (MVMCollectable *)lb->sf,
        "Static Frame");
    MVM_profile_heap_add_collectable_rel_const_cstr(tc, ss, (MVMCollectable *)lb->result,
        "Result");
}

/* Initializes the representation. */
const MVMREPROps * MVMLexotic_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "Lexotic", /* name */
    MVM_REPR_ID_Lexotic,
    0, /* refs_frames */
    NULL, /* unmanaged_size */
    describe_refs,
};
