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
    MVM_exception_throw_adhoc(tc, "Cannot clone an MVMContext");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVM_gc_worklist_add(tc, worklist, &body->context);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVM_free(((MVMContext *)obj)->body.traversals);
}

static MVMint32 apply_traversals(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMuint8 *traversals,
                                 MVMuint32 num_traversals) {
    MVMuint32 i;
    MVMuint32 could_move = 1;
    for (i = 0; i < num_traversals; i++) {
        switch (traversals[i]) {
            case MVM_CTX_TRAV_OUTER:
                could_move = MVM_spesh_frame_walker_move_outer(tc, fw);
                break;
            case MVM_CTX_TRAV_CALLER:
                could_move = MVM_spesh_frame_walker_move_caller(tc, fw);
                break;
            case MVM_CTX_TRAV_OUTER_SKIP_THUNKS:
                could_move = MVM_spesh_frame_walker_move_outer_skip_thunks(tc, fw);
                break;
            case MVM_CTX_TRAV_CALLER_SKIP_THUNKS:
                could_move = MVM_spesh_frame_walker_move_caller_skip_thunks(tc, fw);
                break;
            default:
                MVM_exception_throw_adhoc(tc, "Unrecognized context traversal operation");
        }
        if (!could_move)
            break;
    }
    return could_move;
}

static MVMuint32 setup_frame_walker(MVMThreadContext *tc, MVMSpeshFrameWalker *fw, MVMContextBody *data) {
    MVM_spesh_frame_walker_init(tc, fw, data->context, 0);
    return apply_traversals(tc, fw, data->traversals, data->num_traversals);
}

static void at_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister *result, MVMuint16 kind) {
    MVMString      *name  = (MVMString *)key;
    MVMContextBody *body  = (MVMContextBody *)data;

    MVMSpeshFrameWalker fw;
    MVMRegister *found;
    MVMuint16 found_kind;
    if (!setup_frame_walker(tc, &fw, body) || !MVM_spesh_frame_walker_get_lex(tc, &fw,
            name, &found, &found_kind, 1, NULL)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
            c_name);
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);

    if (found_kind != kind) {
        if (kind == MVM_reg_int64) {
            switch (found_kind) {
                case MVM_reg_int8:
                    result->i64 = found->i8;
                    return;
                case MVM_reg_int16:
                    result->i64 = found->i16;
                    return;
                case MVM_reg_int32:
                    result->i64 = found->i32;
                    return;
            }
        }
        else if (kind == MVM_reg_uint64) {
            switch (found_kind) {
                case MVM_reg_uint8:
                    result->u64 = found->u8;
                    return;
                case MVM_reg_uint16:
                    result->u64 = found->u16;
                    return;
                case MVM_reg_uint32:
                    result->u64 = found->u32;
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
    *result = *found;
}

static void bind_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMRegister value, MVMuint16 kind) {
    MVMString      *name  = (MVMString *)key;
    MVMContextBody *body  = (MVMContextBody *)data;
    MVMFrame       *frame = body->context;

    MVMSpeshFrameWalker fw;
    MVMRegister *found;
    MVMuint16 got_kind;
    MVMFrame *found_frame;
    if (!setup_frame_walker(tc, &fw, body) || !MVM_spesh_frame_walker_get_lex(tc, &fw,
            name, &found, &got_kind, 1, &found_frame)) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' does not exist in this frame",
            c_name);
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);

    if (got_kind != kind) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste,
            "Lexical with name '%s' has a different type in this frame",
                c_name);
    }

    if (got_kind == MVM_reg_obj || got_kind == MVM_reg_str) {
        MVM_ASSIGN_REF(tc, &(found_frame->header), found->o, value.o);
    }
    else {
        *found = value;
    }
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMContextBody *body  = (MVMContextBody *)data;

    MVMSpeshFrameWalker fw;
    MVMuint64 result = setup_frame_walker(tc, &fw, body)
        ? MVM_spesh_frame_walker_get_lexical_count(tc, &fw)
        : 0;
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return result;
}

static MVMint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVMFrame *frame = body->context;

    MVMSpeshFrameWalker fw;
    MVMRegister *found;
    MVMuint16 found_kind;
    MVMuint64 result = setup_frame_walker(tc, &fw, body) && MVM_spesh_frame_walker_get_lex(tc, &fw,
            (MVMString *)key, &found, &found_kind, 0, NULL);
    MVM_spesh_frame_walker_cleanup(tc, &fw);

    return result;
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
    gc_free,
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

/* Walks through the frames and, upon encountering one whose caller has
 * inlines, records the deopt index or JIT location, so we will be able
 * to properly reconstruct the call tree. Stops once it finds a place we
 * already calculated this information. */
static void snapshot_frame_callees(MVMThreadContext *tc, MVMFrame *f) {
    while (f && f->caller) {
        MVMSpeshCandidate *cand = f->caller->spesh_cand;
        if (cand && cand->num_inlines) {
            MVMFrameExtra *extra = MVM_frame_extra(tc, f);
            if (cand->jitcode) {
                if (extra->caller_jit_position)
                    return;
                extra->caller_jit_position = MVM_jit_code_get_current_position(tc, cand->jitcode, f->caller);
            }
            else {
                if (extra->caller_deopt_idx)
                    return;
                /* Store with + 1 to avoid semi-predicate problem. */
                extra->caller_deopt_idx = MVM_spesh_deopt_find_inactive_frame_deopt_idx(tc, f->caller) + 1;
            }
        }
        f = f->caller;
    }
}

/* Creates a MVMContent wrapper object around an MVMFrame. */
MVMObject * MVM_context_from_frame(MVMThreadContext *tc, MVMFrame *f) {
    MVMObject *ctx;
    f = MVM_frame_force_to_heap(tc, f);
    snapshot_frame_callees(tc, f);
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
    MVMuint32 could_move;
    MVM_spesh_frame_walker_init(tc, &fw, base, 0);
    could_move = apply_traversals(tc, &fw, traversals, num_traversals);
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
        memcpy(new_traversals, ctx->body.traversals, ctx->body.num_traversals);
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

/* Resolves the context to an exact frame. Returns NULL if it resolves to an
 * inline or doesn't resolve. */
MVMFrame * MVM_context_get_frame(MVMThreadContext *tc, MVMContext *ctx) {
    MVMSpeshFrameWalker fw;
    MVMFrame *result = NULL;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals))
        result = MVM_spesh_frame_walker_get_frame(tc, &fw);
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return result;
}

/* Resolves the context to an exact frame; if the frame in question is an
 * inline, takes the inline's outer. Returns NULL if neither resolves. */
MVMFrame * MVM_context_get_frame_or_outer(MVMThreadContext *tc, MVMContext *ctx) {
    MVMSpeshFrameWalker fw;
    MVMFrame *result = NULL;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals)) {
        result = MVM_spesh_frame_walker_get_frame(tc, &fw);
        if (!result) {
            MVM_spesh_frame_walker_move_outer(tc, &fw);
            result = MVM_spesh_frame_walker_get_frame(tc, &fw);
        }
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return result;
}

/* Resolves the context and gets a hash of its lexicals. */
MVMObject * MVM_context_lexicals_as_hash(MVMThreadContext *tc, MVMContext *ctx) {
    MVMSpeshFrameWalker fw;
    MVMObject *result;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals))
        result = MVM_spesh_frame_walker_get_lexicals_hash(tc, &fw);
    else
        result = MVM_repr_alloc_init(tc, MVM_hll_current(tc)->slurpy_hash_type);
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return result;
}

/* Find the primitive lexical type of a lexical in the context. */
MVMint64 MVM_context_lexical_primspec(MVMThreadContext *tc, MVMContext *ctx, MVMString *name) {
    MVMSpeshFrameWalker fw;
    MVMint64 primspec = -1;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals))
        primspec = MVM_spesh_frame_walker_get_lexical_primspec(tc, &fw, name);
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    if (primspec < 0) {
        char *c_name = MVM_string_utf8_encode_C_string(tc, name);
        char *waste[] = { c_name, NULL };
        MVM_exception_throw_adhoc_free(tc, waste, "Frame has no lexical with name '%s'",
            c_name);
    }
    return primspec;
}

/* Finds the code object associated with the frame represented by this context. */
MVMObject * MVM_context_get_code(MVMThreadContext *tc, MVMContext *ctx) {
    MVMSpeshFrameWalker fw;
    MVMObject *result = NULL;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals))
        result = MVM_spesh_frame_walker_get_code(tc, &fw);
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return result ? result : tc->instance->VMNull;
}

/* Does a lexical lookup relative to the context's current location.
 * Evaluates to a VMNull if it's not found. */
MVMObject * MVM_context_lexical_lookup(MVMThreadContext *tc, MVMContext *ctx, MVMString *name) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init_for_outers(tc, &fw, ctx->body.context);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals)) {
        MVMRegister *result = MVM_frame_lexical_lookup_using_frame_walker(tc, &fw, name);
        return result ? result->o : tc->instance->VMNull;
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return tc->instance->VMNull;
}

/* Does a dynamic lexical lookup relative to the context's current location.
 * Evaluates to a VMNull if it's not found. */
MVMObject * MVM_context_dynamic_lookup(MVMThreadContext *tc, MVMContext *ctx, MVMString *name) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 0);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals))
        return MVM_frame_getdynlex_with_frame_walker(tc, &fw, name);
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return tc->instance->VMNull;
}

/* Does a caller lexical lookup relative to the context's current location.
 * Evaluates to a VMNull if it's not found. */
MVMObject * MVM_context_caller_lookup(MVMThreadContext *tc, MVMContext *ctx, MVMString *name) {
    MVMSpeshFrameWalker fw;
    MVM_spesh_frame_walker_init(tc, &fw, ctx->body.context, 1);
    if (apply_traversals(tc, &fw, ctx->body.traversals, ctx->body.num_traversals)) {
        MVMRegister *result = MVM_frame_lexical_lookup_using_frame_walker(tc, &fw, name);
        return result ? result->o : tc->instance->VMNull;
    }
    MVM_spesh_frame_walker_cleanup(tc, &fw);
    return tc->instance->VMNull;
}
