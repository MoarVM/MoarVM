#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. Also sets the invocation protocol handler in the STable. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, st, st->WHAT, obj);
        st->size = sizeof(MVMContext);
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
    MVMContextBody *src_body  = (MVMContextBody *)src;
    MVMContextBody *dest_body = (MVMContextBody *)dest;
    MVM_panic(MVM_exitcode_NYI, "MVMContext copy_to NYI");
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVM_gc_worklist_add_frame(tc, worklist, body->context);
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMContext *ctx = (MVMContext *)obj;
    if (ctx->body.context) {
        ctx->body.context = MVM_frame_dec_ref(tc, ctx->body.context);
    }
}

static void * at_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVM_exception_throw_adhoc(tc,
        "MVMContext representation does not support native type storage");
}

#define at_key(tc, st, root, data, _type, member, name) do { \
    MVMContextBody *body = (MVMContextBody *)data; \
    MVMFrame *frame = body->context; \
    MVMObject *result = NULL; \
    MVMLexicalHashEntry *lexical_names = frame->static_info->body.lexical_names, *entry; \
    if (!lexical_names) { \
       MVM_exception_throw_adhoc(tc, \
            "Lexical with name '%s' does not exist in this frame", \
                MVM_string_utf8_encode_C_string(tc, name)); \
    } \
    MVM_HASH_GET(tc, lexical_names, name, entry) \
     \
    if (!entry) { \
       MVM_exception_throw_adhoc(tc, \
            "Lexical with name '%s' does not exist in this frame", \
                MVM_string_utf8_encode_C_string(tc, name)); \
    } \
    if (frame->static_info->body.lexical_types[entry->value] != _type) { \
       MVM_exception_throw_adhoc(tc, \
            "Lexical with name '%s' has a different type in this frame", \
                MVM_string_utf8_encode_C_string(tc, name)); \
    } \
    return frame->env[entry->value].member; \
} while (0)

static MVMObject * at_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMString *name = (MVMString *)key;
    at_key(tc, st, root, data, MVM_reg_obj, o, name);
}

static void bind_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, void *value_addr) {
    MVM_exception_throw_adhoc(tc,
        "MVMContext representation does not support native type storage");
}

static void bind_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMObject *value) {
    MVM_exception_throw_adhoc(tc,
        "MVMContext representation does not yet support bind key boxed");
}

static MVMuint64 elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "MVMContext representation does not support elems");
}

static MVMuint64 exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    MVMContextBody *body = (MVMContextBody *)data;
    MVMFrame *frame = body->context;
    MVMLexicalHashEntry *lexical_names = frame->static_info->body.lexical_names, *entry;
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
    return spec;
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
MVMREPROps * MVMContext_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static MVMREPROps_Associative ass_funcs = {
    at_key_ref,
    at_key_boxed,
    bind_key_ref,
    bind_key_boxed,
    exists_key,
    delete_key,
    get_value_storage_spec,
};

static MVMREPROps this_repr = {
    type_object_for,
    allocate,
    initialize,
    copy_to,
    NULL, /* attr_funcs */
    NULL, /* box_funcs  */
    NULL, /* pos_funcs */
    &ass_funcs,
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
    NULL, /* name */
    0, /* ID */
    0, /* refs_frames */
};
