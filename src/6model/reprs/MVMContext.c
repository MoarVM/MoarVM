#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMContext_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMContext_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMContext);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVM_panic(MVM_exitcode_NYI, "MVMContext copy_to NYI");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->context);
}

static void at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister *result, MVMuint16 kind) {
    MVMString      *name  = (MVMString *)key;
    MVMContextBody *body  = (MVMContextBody *)data;
    MVMFrame       *frame = body->context;
    MVMLexicalRegistry *lexical_names = frame->static_info->body.lexical_names, *entry;
    if (!lexical_names) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
                c_name);
    }
    MVM_HASH_GET(tc, lexical_names, name, entry);
    if (!entry) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
                c_name);
    }
    if (frame->static_info->body.lexical_types[entry->value] != kind) {
        if (kind == MVM_reg_int64) {
            switch (frame->static_info->body.lexical_types[entry->value]) {
                case MVM_reg_int8:
                    result->i64 = frame->env[entry->value].i8;
                    return;
                case MVM_reg_int16:
                    result->i64 = frame->env[entry->value].i16;
                    return;
                case MVM_reg_int32:
                    result->i64 = frame->env[entry->value].i32;
                    return;
            }
        }
        else if (kind == MVM_reg_uint64) {
            switch (frame->static_info->body.lexical_types[entry->value]) {
                case MVM_reg_uint8:
                    result->u64 = frame->env[entry->value].u8;
                    return;
                case MVM_reg_uint16:
                    result->u64 = frame->env[entry->value].u16;
                    return;
                case MVM_reg_uint32:
                    result->u64 = frame->env[entry->value].u32;
                    return;
                case MVM_reg_uint64:
                    result->u64 = frame->env[entry->value].u64;
                    return;
            }

        }
        {
            char *c_name = MVM_string_utf8_encode_C_string(tc, name);
            char *waste[] = { c_name, NULL };
            MVM_exception_throw_adhoc_free(tc, waste,
                "Lexical with name '%s' has a different type in this frame",
                    c_name);
        }
    }
    *result = frame->env[entry->value];
    if (kind == MVM_reg_obj && !result->o)
        result->o = MVM_frame_vivify_lexical(tc, frame, entry->value);
}

static void bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister value, MVMuint16 kind) {
    MVMString      *name  = (MVMString *)key;
    MVMContextBody *body  = (MVMContextBody *)data;
    MVMFrame       *frame = body->context;
    MVMLexicalRegistry *lexical_names = frame->static_info->body.lexical_names, *entry;
    MVMuint16 got_kind;

    if (!lexical_names) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
                c_name);
    }

    MVM_HASH_GET(tc, lexical_names, name, entry);
    if (!entry) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
                c_name);
    }

    got_kind = frame->static_info->body.lexical_types[entry->value];
    if (got_kind != kind) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' has a different type in this frame",
                c_name);
    }

    if (got_kind == MVM_reg_obj || got_kind == MVM_reg_str) {
        MVM_ASSIGN_REF(tc, &(frame->header), frame->env[entry->value].o, value.o);
    }
    else {
        frame->env[entry->value] = value;
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMContextBody *body  = (MVMContextBody *)data;
    MVMFrame       *frame = body->context;
    MVMLexicalRegistry *lexical_names = frame->static_info->body.lexical_names;
    return (MVMuint64) HASH_CNT(hash_handle, lexical_names);
}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVMFrame *frame = body->context;
    MVMLexicalRegistry *lexical_names = frame->static_info->body.lexical_names, *entry;
    MVMString *name = (MVMString *)key;
    if (!lexical_names)
        return 0;
    MVM_HASH_GET(tc, lexical_names, name, entry);
    return entry ? 1 : 0;
}

static void delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVM_exception_throw_adhoc(tc,
        "MVMContext representation does not support delete key");
}

static MVMStorageSpec get_value_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    spec.bits            = 0;
    spec.align           = 0;
    spec.is_unsigned     = 0;

    return spec;
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
const MVMREPROps * MVMContext_initialize(MVMThreadContext *tc) {
    return &MVMContext_this_repr;
}

static const MVMREPROps MVMContext_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    {
        at_key,
        bind_key,
        exists_key,
        delete_key,
        get_value_storage_spec
    },   /* ass_funcs */
    elems,
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
    "MVMContext", /* name */
    MVM_REPR_ID_MVMContext,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};

/* Creates a MVMContent wrapper object around an MVMFrame. */
MVMObject * MVM_context_from_frame(MVMThreadContext *tc, MVMFrame *f) {
    MVMObject *ctx;
    f = MVM_frame_force_to_heap(tc, f);
    MVMROOT(tc, f, {
        ctx = MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
        MVM_ASSIGN_REF(tc, &(ctx->header), ((MVMContext *)ctx)->body.context, f);
    });
    return ctx;
}

/* Checks if we can perform a traversal and reach an existing frame. */
static MVMint32 traversal_exists(MVMThreadContext *tc, MVMFrame *base, MVMuint8 *traversals,
                                 MVMuint32 num_traversals) {
    MVMSpeshFrameWalker fw;
    MVMuint32 i;
    MVMuint32 could_move = 1;
    MVM_spesh_frame_walker_init(tc, &fw, base, 0);
    for (i = 0; i < num_traversals; i++) {
        switch (traversals[i]) {
            case MVM_CTX_TRAV_OUTER:
                could_move = MVM_spesh_frame_walker_move_outer(tc, &fw);
                break;
            case MVM_CTX_TRAV_CALLER:
                could_move = MVM_spesh_frame_walker_move_caller(tc, &fw);
                break;
            case MVM_CTX_TRAV_OUTER_SKIP_THUNKS:
                could_move = MVM_spesh_frame_walker_move_outer_skip_thunks(tc, &fw);
                break;
            case MVM_CTX_TRAV_CALLER_SKIP_THUNKS:
                could_move = MVM_spesh_frame_walker_move_caller_skip_thunks(tc, &fw);
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Unrecognized context traversal operation");
        }
        if (!could_move)
            break;
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return could_move;
}

/* Try to get a context with the specified traversal applied. Ensures that the
 * traversal would lead to a result, and returns a NULL context if not. */
MVMObject * MVM_context_apply_traversal(MVMThreadContext *tc, MVMContext *ctx, MVMuint8 traversal) {
    /* Create new traversals array with the latest one at the end. */
    MVMuint32 new_num_traversals = ctx->body.num_traversals + 1;
    MVMuint8 *new_traversals = MVM_malloc(new_num_traversals);
    if (ctx->body.num_traversals)
        memcpy(new_traversals, ctx->body.traversals, ctx->body.num_traversals + 1);
    new_traversals[new_num_traversals - 1] = traversal;

    /* Verify that we can do this traversal. */
    if (traversal_exists(tc, ctx->body.context, new_traversals, new_num_traversals)) {
        /* Yes, make a new context object and return it. */
        MVMContext *result;
        MVMROOT(tc, ctx, {
            result = (MVMContext *)MVM_repr_alloc_init(tc, tc->instance->boot_types.BOOTContext);
        });
        MVM_ASSIGN_REF(tc, &(result->common.header), result->body.context, ctx->body.context);
        result->body.traversals = new_traversals;
        result->body.num_traversals = new_num_traversals;
        return (MVMObject *)result;
    }
    else {
        MVM_free(new_traversals);
        return tc->instance->VMNull;
    }
}
