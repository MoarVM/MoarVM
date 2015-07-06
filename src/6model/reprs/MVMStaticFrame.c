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

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMStaticFrameBody *src_body  = (MVMStaticFrameBody *)src;
    MVMStaticFrameBody *dest_body = (MVMStaticFrameBody *)dest;

    if (!src_body->fully_deserialized)
        MVM_exception_throw_adhoc(tc, "Can only clone a fully deserialized MVMFrame");

    dest_body->orig_bytecode = src_body->orig_bytecode;
    dest_body->bytecode_size = src_body->bytecode_size;
    if (src_body->bytecode == src_body->orig_bytecode) {
        /* Easy - the source MVMStaticFrameBody doesn't own the memory. */
        dest_body->bytecode = src_body->bytecode;
    }
    else {
        /* We're going to need to make a copy, in case the source object gets
           GC'd before we do, and so they free memory we point to. */
        /* If this gets to be a resource hog, then implement something more
           sophisticated. The easiest thing would be to bump the allocated size
           and value stored in bytecode by sizeof(MVMuint64), and use the extra
           space to store a reference count. */
        dest_body->bytecode = MVM_malloc(src_body->bytecode_size);
        memcpy(dest_body->bytecode, src_body->bytecode, src_body->bytecode_size);
    }

    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->cu, src_body->cu);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->cuuid, src_body->cuuid);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->name, src_body->name);
    MVM_ASSIGN_REF(tc, &(dest_root->header), dest_body->static_code, src_body->static_code);

    dest_body->num_locals = src_body->num_locals;
    dest_body->num_lexicals = src_body->num_lexicals;
    {
        MVMuint16 *local_types = MVM_malloc(sizeof(MVMuint16) * src_body->num_locals);
        MVMuint16 *lexical_types = MVM_malloc(sizeof(MVMuint16) * src_body->num_lexicals);
        memcpy(local_types, src_body->local_types, sizeof(MVMuint16) * src_body->num_locals);
        memcpy(lexical_types, src_body->lexical_types, sizeof(MVMuint16) * src_body->num_lexicals);
        dest_body->local_types = local_types;
        dest_body->lexical_types = lexical_types;
    }
    {
        MVMLexicalRegistry *current, *tmp;
        unsigned bucket_tmp;

        /* NOTE: if we really wanted to, we could avoid rehashing... */
        HASH_ITER(hash_handle, src_body->lexical_names, current, tmp, bucket_tmp) {
            size_t klen;
            void *kdata;
            MVMLexicalRegistry *new_entry = MVM_malloc(sizeof(MVMLexicalRegistry));

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

        dest_body->static_env = MVM_malloc(src_body->env_size);
        memcpy(dest_body->static_env, src_body->static_env, src_body->env_size);
        dest_body->static_env_flags = MVM_malloc(src_body->num_lexicals);
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
    dest_body->handlers     = MVM_malloc(src_body->num_handlers * sizeof(MVMFrameHandler));
    memcpy(dest_body->handlers, src_body->handlers, src_body->num_handlers * sizeof(MVMFrameHandler));
    dest_body->instrumentation_level = 0;
    dest_body->pool_index            = src_body->pool_index;
    dest_body->num_annotations       = src_body->num_annotations;
    dest_body->annotations_data      = src_body->annotations_data;
    dest_body->fully_deserialized    = 1;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;
    MVMLexicalRegistry *current, *tmp;
    unsigned bucket_tmp;

    /* mvmobjects */
    MVM_gc_worklist_add(tc, worklist, &body->cu);
    MVM_gc_worklist_add(tc, worklist, &body->cuuid);
    MVM_gc_worklist_add(tc, worklist, &body->name);
    MVM_gc_worklist_add(tc, worklist, &body->outer);
    MVM_gc_worklist_add(tc, worklist, &body->static_code);

    /* If it's not fully deserialized, none of the following can apply. */
    if (!body->fully_deserialized)
        return;

    /* lexical names hash keys */
    HASH_ITER(hash_handle, body->lexical_names, current, tmp, bucket_tmp) {
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

    /* Spesh slots. */
    if (body->num_spesh_candidates) {
        MVMint32 i, j;
        for (i = 0; i < body->num_spesh_candidates; i++) {
            for (j = 0; j < body->spesh_candidates[i].num_guards; j++)
                MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i].guards[j].match);
            for (j = 0; j < body->spesh_candidates[i].num_spesh_slots; j++)
                MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i].spesh_slots[j]);
            if (body->spesh_candidates[i].log_slots)
                for (j = 0; j < body->spesh_candidates[i].num_log_slots * MVM_SPESH_LOG_RUNS; j++)
                    MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i].log_slots[j]);
            for (j = 0; j < body->spesh_candidates[i].num_inlines; j++)
                MVM_gc_worklist_add(tc, worklist, &body->spesh_candidates[i].inlines[j].code);
            if (body->spesh_candidates[i].sg)
                MVM_spesh_graph_mark(tc, body->spesh_candidates[i].sg, worklist);
        }
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMStaticFrame *sf = (MVMStaticFrame *)obj;
    MVMStaticFrameBody *body = &sf->body;
    if (body->orig_bytecode != body->bytecode) {
        MVM_free(body->bytecode);
        body->bytecode = body->orig_bytecode;
    }

    /* If it's not fully deserialized, none of the following can apply. */
    if (!body->fully_deserialized)
        return;
    MVM_checked_free_null(body->instr_offsets);
    MVM_checked_free_null(body->handlers);
    MVM_checked_free_null(body->static_env);
    MVM_checked_free_null(body->static_env_flags);
    MVM_checked_free_null(body->local_types);
    MVM_checked_free_null(body->lexical_types);
    MVM_checked_free_null(body->lexical_names_list);
    MVM_HASH_DESTROY(hash_handle, MVMLexicalRegistry, body->lexical_names);
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
    /* XXX in the end we'll support inlining of this... */
    return &storage_spec;
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
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    NULL, /* spesh */
    "MVMStaticFrame", /* name */
    MVM_REPR_ID_MVMStaticFrame,
    0, /* refs_frames */
};
