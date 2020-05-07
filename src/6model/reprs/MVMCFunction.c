#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMCFunction_this_repr;

/* Temporary workaround. Very temporary, given it's totally leaky. */
static void adapt_legacy_args(MVMThreadContext *tc, MVMCallsite **callsite, MVMRegister **args) {
    MVMuint8 tweaked = 0;
    if ((*callsite)->has_flattening) {
        MVMArgProcContext ctx;
        MVM_args_proc_init(tc, &ctx, *callsite, *args);
        MVM_args_checkarity(tc, &ctx, 0, 1024); /* Forces flattening */
        *callsite = MVM_args_copy_callsite(tc, &ctx);
        *args = ctx.legacy.args;
        (*callsite)->arg_names = MVM_calloc(
                (*callsite)->flag_count - (*callsite)->num_pos,
                sizeof(MVMString *));
        tweaked = 1;
    }
    MVMuint16 insert_pos = (*callsite)->num_pos;
    MVMuint16 from_pos = insert_pos + 1;
    while (insert_pos < (*callsite)->flag_count) {
        if (tweaked)
            (*callsite)->arg_names[insert_pos - (*callsite)->num_pos] = (*args)[from_pos - 1].s;
        (*args)[insert_pos] = (*args)[from_pos];
        insert_pos++;
        from_pos += 2;
    }
}

/* Legacy invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    if (IS_CONCRETE(invokee)) {
        MVMArgs arg_info;
        MVM_gc_allocate_gen2_default_set(tc);
        adapt_legacy_args(tc, &callsite, &args);
        arg_info.callsite = callsite;
        arg_info.source = args;
        arg_info.map = MVM_args_identity_map(tc, callsite);
        ((MVMCFunction *)invokee)->body.func(tc, arg_info);
        MVM_gc_allocate_gen2_default_clear(tc);
    }
    else {
        MVM_exception_throw_adhoc(tc, "Cannot invoke C function type object");
    }
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMCFunction_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMCFunction);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCFunctionBody *src_body  = (MVMCFunctionBody *)src;
    MVMCFunctionBody *dest_body = (MVMCFunctionBody *)dest;
    dest_body->func = src_body->func;
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

/* Initializes the representation. */
const MVMREPROps * MVMCFunction_initialize(MVMThreadContext *tc) {
    return &MVMCFunction_this_repr;
}

static const MVMREPROps MVMCFunction_this_repr = {
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
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMCFunction", /* name */
    MVM_REPR_ID_MVMCFunction,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
