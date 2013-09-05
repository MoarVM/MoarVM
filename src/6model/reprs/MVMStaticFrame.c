#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Invocation protocol handler. */
static void invoke_handler(MVMThreadContext *tc, MVMObject *invokee, MVMCallsite *callsite, MVMRegister *args) {
    MVM_exception_throw_adhoc(tc, "Cannot invoke static frame object");
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;

    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVMROOT(tc, st, {
        obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->invoke = invoke_handler;
        st->size = sizeof(MVMStaticFrame);
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
    MVMStaticFrameBody *src_body  = (MVMStaticFrameBody *)src;
    MVMStaticFrameBody *dest_body = (MVMStaticFrameBody *)dest;
    
    dest_body->bytecode = src_body->bytecode;
    dest_body->bytecode_size = src_body->bytecode_size;
    MVM_ASSIGN_REF(tc, dest_root, dest_body->cu, src_body->cu);
    MVM_ASSIGN_REF(tc, dest_root, dest_body->cuuid, src_body->cuuid);
    MVM_ASSIGN_REF(tc, dest_root, dest_body->name, src_body->name);
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
        MVMLexicalHashEntry *current, *tmp;

        /* NOTE: if we really wanted to, we could avoid rehashing... */
        HASH_ITER(hash_handle, src_body->lexical_names, current, tmp) {
            size_t klen;
            void *kdata;
            MVMLexicalHashEntry *new_entry = malloc(sizeof(MVMLexicalHashEntry));

            /* don't need to clone the string */
            MVM_ASSIGN_REF(tc, dest_root, new_entry->key, current->key);
            new_entry->value = current->value;

            MVM_HASH_EXTRACT_KEY(tc, &kdata, &klen, current->key, "really broken")
            HASH_ADD_KEYPTR(hash_handle, dest_body->lexical_names, kdata, klen, new_entry);
        }
    }

    /* XXX start out with blank static env? */
    dest_body->static_env = calloc(1, src_body->env_size);
    dest_body->env_size = src_body->env_size;
    dest_body->invoked = 0;
    dest_body->work_size = src_body->work_size;
    dest_body->prior_invocation = NULL; /* XXX ? */
    dest_body->num_handlers = src_body->num_handlers;
    dest_body->handlers = malloc(src_body->num_handlers * sizeof(MVMFrameHandler));
    memcpy(dest_body->handlers, src_body->handlers, src_body->num_handlers * sizeof(MVMFrameHandler));
    if (src_body->outer)
        MVM_ASSIGN_REF(tc, dest_root, dest_body->outer, src_body->outer);
    dest_body->static_code = NULL; /* XXX ? */
    dest_body->pool_index = src_body->pool_index;
    dest_body->num_annotations = src_body->num_annotations;
    dest_body->annotations_data = src_body->annotations_data;
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStaticFrameBody *body = (MVMStaticFrameBody *)data;
    MVMLexicalHashEntry *current, *tmp;

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

    /* prior invocation */
    MVM_gc_worklist_add_frame(tc, worklist, body->prior_invocation);

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
    MVM_checked_free_null(body->local_types);
    MVM_checked_free_null(body->lexical_types);
    MVM_checked_free_null(body->lexical_names_list);
    {
        MVMLexicalHashEntry *current, *tmp;

        /* The macros already check for null. Also, must not delete the head
         * node until after calling clear, or we look into freed memory. */
        HASH_ITER(hash_handle, body->lexical_names, current, tmp) {
            if (current != body->lexical_names)
                free(current);
        }
        HASH_CLEAR(hash_handle, body->lexical_names);
        MVM_checked_free_null(body->lexical_names);
    }
    if (body->prior_invocation) {
        body->prior_invocation = MVM_frame_dec_ref(tc, body->prior_invocation);
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
MVMREPROps * MVMStaticFrame_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
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
    return this_repr;
}
