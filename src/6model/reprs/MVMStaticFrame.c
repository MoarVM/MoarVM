#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMStaticFrame_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMStaticFrame_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
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
    if (dest_body->num_locals) {
        MVMuint16 *local_types = MVM_malloc(sizeof(MVMuint16) * src_body->num_locals);
        memcpy(local_types, src_body->local_types, sizeof(MVMuint16) * src_body->num_locals);
        dest_body->local_types = local_types;
    }
    else {
        dest_body->local_types = NULL;
    }

    MVMuint32 num_lexicals = src_body->num_lexicals;
    dest_body->num_lexicals = num_lexicals;
    if (num_lexicals) {
        MVMuint16 *lexical_types = MVM_malloc(sizeof(MVMuint16) * src_body->num_lexicals);
        memcpy(lexical_types, src_body->lexical_types,
               sizeof(MVMuint16) * src_body->num_lexicals);

        MVMString **lexical_names_list = MVM_malloc(sizeof(MVMString *) * src_body->num_lexicals);
        for (MVMuint32 j = 0; j < num_lexicals; j++) {
            /* don't need to clone the string */
            MVM_ASSIGN_REF(tc, &(dest_root->header), lexical_names_list[j], src_body->lexical_names_list[j]);
        }
        /* This correctly handles the case where lexical_names.table is NULL */
        MVM_index_hash_shallow_copy(tc, &src_body->lexical_names, &dest_body->lexical_names);

        dest_body->lexical_names_list = lexical_names_list;
        dest_body->lexical_types = lexical_types;
    }
    else {
        dest_body->lexical_names_list = NULL;
        dest_body->lexical_types = NULL;
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
    if (src_body->num_handlers) {
        dest_body->handlers = MVM_malloc(src_body->num_handlers * sizeof(MVMFrameHandler));
        memcpy(dest_body->handlers, src_body->handlers,
            src_body->num_handlers * sizeof(MVMFrameHandler));
    }
    else {
        dest_body->handlers = NULL;
    }
    dest_body->instrumentation_level = 0;
    dest_body->num_annotations       = src_body->num_annotations;
    dest_body->annotations_data      = src_body->annotations_data;
    dest_body->fully_deserialized    = 1;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;

    /* mvmobjects */
    MVM_gc_worklist_add(tc, worklist, &body->cu);
    MVM_gc_worklist_add(tc, worklist, &body->cuuid);
    MVM_gc_worklist_add(tc, worklist, &body->name);
    MVM_gc_worklist_add(tc, worklist, &body->outer);
    MVM_gc_worklist_add(tc, worklist, &body->static_code);

    /* If it's not fully deserialized, none of the following can apply. */
    if (!body->fully_deserialized)
        return;

    /* lexical names */
    MVMuint32 num_lexicals = body->num_lexicals;
    MVMString **lexical_names_list = body->lexical_names_list;

    for (MVMuint32 j = 0; j < num_lexicals; j++) {
        MVM_gc_worklist_add(tc, worklist, &lexical_names_list[j]);
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

    /* Inline cache and spesh. */
    MVM_disp_inline_cache_mark(tc, &(body->inline_cache), worklist);
    MVM_gc_worklist_add(tc, worklist, &body->spesh);

    /* Debug symbols. */
    if (body->instrumentation) {
        MVMStrHashTable *const debug_locals = &body->instrumentation->debug_locals;
        MVMStrHashIterator iterator = MVM_str_hash_first(tc, debug_locals);
        while (!MVM_str_hash_at_end(tc, debug_locals, iterator)) {
            MVMStaticFrameDebugLocal *local = MVM_str_hash_current_nocheck(tc, debug_locals, iterator);
            MVM_gc_worklist_add(tc, worklist, &local->hash_handle.key);
            iterator = MVM_str_hash_next_nocheck(tc, debug_locals, iterator);
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
    MVM_free(body->handlers);
    MVM_free(body->work_initial);
    MVM_free(body->static_env);
    MVM_free(body->static_env_flags);
    MVM_free(body->local_types);
    MVM_free(body->lexical_types);
    MVM_free(body->lexical_names_list);
    MVM_free(body->instrumentation);
    MVM_index_hash_demolish(tc, &body->lexical_names);
    MVM_disp_inline_cache_destroy(tc, &(body->inline_cache));
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

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;
    MVMuint64 size = 0;

    if (body->fully_deserialized) {
        size += sizeof(MVMuint16) * body->num_locals;
        size += sizeof(MVMuint16) * body->num_lexicals;

        if (body->bytecode != body->orig_bytecode)
            size += body->bytecode_size;

        size += sizeof(MVMString *) * body->num_lexicals;

        size += MVM_index_hash_allocated_size(tc, &body->lexical_names);

        size += sizeof(MVMFrameHandler) * body->num_handlers;

        /* XXX i *think* the annotations are just a pointer into the serialized
         * blob, so don't actually count it towards the unmanaged size. */
        /*
        size += sizeof(MVMuint8) * body->num_annotations
        */
        size += body->env_size; /* static_env */
        size += body->num_lexicals; /* static_env_flags */

        if (body->instrumentation) {
            size += body->instrumentation->uninstrumented_bytecode_size;
            size += body->instrumentation->instrumented_bytecode_size;

            /* XXX not 100% sure if num_handlers from the body is also the
             * number of handlers in instrumented version. should be, though. */
            size += sizeof(MVMFrameHandler) * body->num_handlers * 2;
        }
    }

    return size;
}

static void describe_refs(MVMThreadContext *tc, MVMHeapSnapshotState *ss, MVMSTable *st, void *data) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;

    static MVMuint64 cache_1 = 0;
    static MVMuint64 cache_2 = 0;
    static MVMuint64 cache_3 = 0;
    static MVMuint64 cache_4 = 0;
    static MVMuint64 cache_5 = 0;
    static MVMuint64 cache_6 = 0;

    MVMuint64 nonstatic_cache_1 = 0;

    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->cu, "Compilation Unit", &cache_1);
    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->cuuid, "Compilation Unit Unique ID", &cache_2);
    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->name, "Name", &cache_3);
    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->outer, "Outer static frame", &cache_4);
    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->static_code, "Static code object", &cache_5);

    /* If it's not fully deserialized, none of the following can apply. */
    if (!body->fully_deserialized)
        return;

    /* lexical names */
    MVMuint32 num_lexicals = body->num_lexicals;
    MVMString **lexical_names_list = body->lexical_names_list;

    for (MVMuint32 j = 0; j < num_lexicals; j++) {
        MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
            (MVMCollectable *)lexical_names_list[j], "Lexical name", &nonstatic_cache_1);
    }

    /* static env */
    if (body->static_env) {
        MVMuint16 *type_map = body->lexical_types;
        MVMuint16  count    = body->num_lexicals;
        MVMuint16  i;
        for (i = 0; i < count; i++)
            if (type_map[i] == MVM_reg_str || type_map[i] == MVM_reg_obj)
                MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
                    (MVMCollectable *)body->static_env[i].o, "Static Environment Entry", &cache_6);
    }

    /* Spesh data */
    MVM_profile_heap_add_collectable_rel_const_cstr_cached(tc, ss,
        (MVMCollectable *)body->spesh, "Specializer Data", &cache_6);
}

/* Initializes the representation. */
const MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc) {
    return &MVMStaticFrame_this_repr;
}

static const MVMREPROps MVMStaticFrame_this_repr = {
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
    unmanaged_size, /* unmanaged_size */
    describe_refs,
};


char * MVM_staticframe_file_location(MVMThreadContext *tc, MVMStaticFrame *sf) {
    MVMBytecodeAnnotation *ann = MVM_bytecode_resolve_annotation(tc, &sf->body, 0);
    MVMCompUnit *cu = sf->body.cu;
    MVMuint32          str_idx = ann ? ann->filename_string_heap_index : 0;
    MVMint32           line_nr = ann ? ann->line_number : 1;
    MVMString        *filename = cu->body.filename;
    char        *filename_utf8 = "<unknown>";
    char               *result = MVM_malloc(1024);
    if (ann && str_idx < cu->body.num_strings)
        filename = MVM_cu_string(tc, cu, str_idx);
    if (filename)
        filename_utf8 = MVM_string_utf8_encode_C_string(tc, filename);
    snprintf(result, 1023, "%s:%d", filename_utf8, line_nr);
    if (filename)
        MVM_free(filename_utf8);
    return result;
}

/* We could change this code (and bytecode.c) to lazily only build the lookup
 * hash on the first lookup. I don't have a feel for how often no lookups are
 * made, and hence whether the added complexity would be much of a saving. */
MVMuint32 MVM_get_lexical_by_name(MVMThreadContext *tc, MVMStaticFrame *sf, MVMString *name) {
    /* deserialize_frames in bytecode.c doesn't create the lookup hash if there
     * are only a small number of lexicals in this frame. */
    if (MVM_index_hash_built(tc, &sf->body.lexical_names)) {
        return MVM_index_hash_fetch(tc, &sf->body.lexical_names, sf->body.lexical_names_list, name);
    }

    MVMString **lexical_names_list = sf->body.lexical_names_list;
    MVMuint32 num_lexicals = sf->body.num_lexicals;
    for (MVMuint32 j = 0; j < num_lexicals; j++) {
        if (MVM_string_equal(tc, name, lexical_names_list[j]))
            return j;
    }
    return MVM_INDEX_HASH_NOT_FOUND;
}
