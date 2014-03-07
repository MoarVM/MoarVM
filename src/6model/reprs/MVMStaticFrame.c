#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "Cannot invoke static frame object");
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMStaticFrame);
    });

    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    return MVM_gc_allocate_object(tc, st);
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMStaticFrameBody *src_body  = (MVMStaticFrameBody *)src;
    MVMStaticFrameBody *dest_body = (MVMStaticFrameBody *)dest;

    dest_body->bytecode = src_body->bytecode;
    dest_body->bytecode_size = src_body->bytecode_size;

    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->cu, src_body->cu);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->cuuid, src_body->cuuid);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->name, src_body->name);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->static_code, src_body->static_code);

    dest_body->num_locals = src_body->num_locals;
    dest_body->num_lexicals = src_body->num_lexicals;
    {
        MVMuint16 *local_types = malloc(sizeof(MVMuint16) * src_body->num_locals);
        MVMuint16 *lexical_types = malloc(sizeof(MVMuint16) * src_body->num_lexicals);
        memcpy(local_types, src_body->local_types, sizeof(MVMuint16) * src_body->num_locals);
        memcpy(lexical_types, src_body->lexical_types, sizeof(MVMuint16) * src_body->num_lexicals);
        dest_body->local_types = local_types;
        dest_body->lexical_types = lexical_types;
    }
    {
        MVMLexicalRegistry *current, *tmp;

        /* NOTE: if we really wanted to, we could avoid rehashing... */
        HASH_ITER(hash_handle, src_body->lexical_names, current, tmp) {
            size_t klen;
            void *kdata;
            MVMLexicalRegistry *new_entry = malloc(sizeof(MVMLexicalRegistry));

            /* don't need to clone the string */
            MVM_ASSIGN_REF(tc, &(dest_root->header), new_entry->key, current->key);
            new_entry->value = current->value;

            MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, current->key, "really broken")
            HASH_ADD_KEYPTR(hash_handle, dest_body->lexical_names, kdata, klen, new_entry);
        }
    }

    /* Static environment needs to be copied, and any objects WB'd. */
    if (src_body->env_size) {
        MVMuint16 *type_map = src_body->lexical_types;
        MVMuint16  count    = src_body->num_lexicals;
        MVMuint16  i;

        dest_body->static_env = malloc(src_body->env_size);
        memcpy(dest_body->static_env, src_body->static_env, src_body->env_size);
        dest_body->static_env_flags = malloc(src_body->num_lexicals);
        memcpy(dest_body->static_env_flags, src_body->static_env_flags, src_body->num_lexicals);

        for (i = 0; i < count; i++) {
            if (type_map[i] == MVM_reg_str) {
                MVM_gc_write_barrier(tc, (MVMCollectable *)dest_root, (MVMCollectable *)dest_body->static_env[i].s);
            }
            else if (type_map[i] == MVM_reg_obj) {
                MVM_gc_write_barrier(tc, (MVMCollectable *)dest_root, (MVMCollectable *)dest_body->static_env[i].o);
            }
        }
    }
    dest_body->env_size = src_body->env_size;
    dest_body->work_size = src_body->work_size;

    if (src_body->outer)
        MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->outer, src_body->outer);

    dest_body->num_handlers = src_body->num_handlers;
    dest_body->handlers = malloc(src_body->num_handlers * sizeof(MVMFrameHandler));
    memcpy(dest_body->handlers, src_body->handlers, src_body->num_handlers * sizeof(MVMFrameHandler));
    dest_body->invoked = 0;
    dest_body->pool_index = src_body->pool_index;
    dest_body->num_annotations = src_body->num_annotations;
    dest_body->annotations_data = src_body->annotations_data;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;
    MVMLexicalRegistry *current, *tmp;

    /* mvmobjects */
    MVM_gc_worklist_add(tc, worklist, &body->cu);
    MVM_gc_worklist_add(tc, worklist, &body->cuuid);
    MVM_gc_worklist_add(tc, worklist, &body->name);
    MVM_gc_worklist_add(tc, worklist, &body->outer);
    MVM_gc_worklist_add(tc, worklist, &body->static_code);

    /* lexical names hash keys */
    HASH_ITER(hash_handle, body->lexical_names, current, tmp) {
        MVM_gc_worklist_add(tc, worklist, &current->key);
    }

    /* static env */
    if (body->static_env) {
        MVMuint16 *type_map = body->lexical_types;
        MVMuint16  count    = body->num_lexicals;
        MVMuint16  i;
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_gc_worklist_add(tc, worklist, &body->static_env[i].o);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMStaticFrame *sf = (MVMStaticFrame *)obj;
    MVMStaticFrameBody *body = &sf->body;
    MVM_checked_free_null(body->handlers);
    MVM_checked_free_null(body->static_env);
    MVM_checked_free_null(body->static_env_flags);
    MVM_checked_free_null(body->local_types);
    MVM_checked_free_null(body->lexical_types);
    MVM_checked_free_null(body->lexical_names_list);
    MVM_checked_free_null(body->instr_offsets);
    MVM_HASH_DESTROY(hash_handle, MVMLexicalRegistry, body->lexical_names);
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
const MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    allocate,
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
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "MVMStaticFrame", /* name */
    MVM_REPR_ID_MVMStaticFrame,
    0, /* refs_frames */
};
