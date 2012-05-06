#include "moarvm.h"

/* Default REPR function handlers. */
MVM_NO_RETURN
static void die_no_attrs(MVMThreadContext *tc, MVMString *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation does not support attribute storage");
}
static MVMObject * default_get_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc, st->REPR->name);
}
static void * default_get_attribute_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc, st->REPR->name);
}
static void default_bind_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, MVMObject *value) {
    die_no_attrs(tc, st->REPR->name);
}
static void default_bind_attribute_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint, void *value) {
    die_no_attrs(tc, st->REPR->name);
}
static MVMint32 default_is_attribute_initialized(MVMThreadContext *tc, MVMSTable *st, void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    die_no_attrs(tc, st->REPR->name);
}
static MVMint64 default_hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_handle, MVMString *name) {
    return MVM_NO_HINT;
}
static void default_set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot box a native int");
}
static MVMint64 default_get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot unbox to a native int");
}
static void default_set_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMnum64 value) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot box a native num");
}
static MVMnum64 default_get_num(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot unbox to a native num");
}
static void default_set_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMString *value) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot box a native string");
}
static MVMString * default_get_str(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot unbox to a native string");
}
static void * default_get_boxed_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint32 repr_id) {
    MVM_exception_throw_adhoc(tc,
        "This representation cannot unbox to other types");
}
MVM_NO_RETURN
static void die_no_pos(MVMThreadContext *tc, MVMString *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation does not support positional access");
}
static void * default_at_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index) {
    die_no_pos(tc, st->REPR->name);
}
static MVMObject * default_at_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index) {
    die_no_pos(tc, st->REPR->name);
}
static void default_bind_pos_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index, void *addr) {
    die_no_pos(tc, st->REPR->name);
}
static void default_bind_pos_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 index, MVMObject *obj) {
    die_no_pos(tc, st->REPR->name);
}
static MVMuint64 default_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    die_no_pos(tc, st->REPR->name);
}
static void default_preallocate(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name);
}
static void default_trim_to(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name);
}
static void default_make_hole(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name);
}
static void default_delete_elems(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 at_index, MVMuint64 count) {
    die_no_pos(tc, st->REPR->name);
}
static MVMStorageSpec default_get_elem_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    die_no_pos(tc, st->REPR->name);
}
MVM_NO_RETURN
static void die_no_ass(MVMThreadContext *tc, MVMString *repr_name) {
    MVM_exception_throw_adhoc(tc,
        "This representation does not support associative access");
}
void * default_at_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
MVMObject * default_at_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
void default_bind_key_ref(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, void *value_addr) {
    die_no_ass(tc, st->REPR->name);
}
void default_bind_key_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key, MVMObject *value) {
    die_no_ass(tc, st->REPR->name);
}
MVMuint64 default_elems_ass(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    die_no_ass(tc, st->REPR->name);
}
MVMuint64 default_exists_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
void default_delete_key(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMObject *key) {
    die_no_ass(tc, st->REPR->name);
}
MVMStorageSpec default_get_value_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    die_no_ass(tc, st->REPR->name);
}

/* Set default attribute functions on a REPR that lacks them. */
static void add_default_attr_funcs(MVMThreadContext *tc, MVMREPROps *repr) {
    repr->attr_funcs = malloc(sizeof(MVMREPROps_Attribute));
    repr->attr_funcs->get_attribute_boxed = default_get_attribute_boxed;
    repr->attr_funcs->get_attribute_ref = default_get_attribute_ref;
    repr->attr_funcs->bind_attribute_boxed = default_bind_attribute_boxed;
    repr->attr_funcs->bind_attribute_ref = default_bind_attribute_ref;
    repr->attr_funcs->is_attribute_initialized = default_is_attribute_initialized;
    repr->attr_funcs->hint_for = default_hint_for;
}

/* Set default boxing functions on a REPR that lacks them. */
static void add_default_box_funcs(MVMThreadContext *tc, MVMREPROps *repr) {
    repr->box_funcs = malloc(sizeof(MVMREPROps_Boxing));
    repr->box_funcs->set_int = default_set_int;
    repr->box_funcs->get_int = default_get_int;
    repr->box_funcs->set_num = default_set_num;
    repr->box_funcs->get_num = default_get_num;
    repr->box_funcs->set_str = default_set_str;
    repr->box_funcs->get_str = default_get_str;
    repr->box_funcs->get_boxed_ref = default_get_boxed_ref;
}

/* Set default positional functions on a REPR that lacks them. */
static void add_default_pos_funcs(MVMThreadContext *tc, MVMREPROps *repr) {
    repr->pos_funcs = malloc(sizeof(MVMREPROps_Positional));
    repr->pos_funcs->at_pos_ref = default_at_pos_ref;
    repr->pos_funcs->at_pos_boxed = default_at_pos_boxed;
    repr->pos_funcs->bind_pos_ref = default_bind_pos_ref;
    repr->pos_funcs->bind_pos_boxed = default_bind_pos_boxed;
    repr->pos_funcs->elems = default_elems;
    repr->pos_funcs->preallocate = default_preallocate;
    repr->pos_funcs->trim_to = default_trim_to;
    repr->pos_funcs->make_hole = default_make_hole;
    repr->pos_funcs->delete_elems = default_delete_elems;
    repr->pos_funcs->get_elem_storage_spec = default_get_elem_storage_spec;
}

/* Set default associative functions on a REPR that lacks them. */
static void add_default_ass_funcs(MVMThreadContext *tc, MVMREPROps *repr) {
    repr->ass_funcs = malloc(sizeof(MVMREPROps_Associative));
    repr->ass_funcs->at_key_ref = default_at_key_ref;
    repr->ass_funcs->at_key_boxed = default_at_key_boxed;
    repr->ass_funcs->bind_key_ref = default_bind_key_ref;
    repr->ass_funcs->bind_key_boxed = default_bind_key_boxed;
    repr->ass_funcs->elems = default_elems_ass;
    repr->ass_funcs->exists_key = default_exists_key;
    repr->ass_funcs->delete_key = default_delete_key;
    repr->ass_funcs->get_value_storage_spec = default_get_value_storage_spec;
}

/* Registers a representation. It this is ever made public, it should first be
 * made thread-safe. */
static void register_repr(MVMThreadContext *tc, MVMString *name, MVMREPROps *repr) {
    /* Allocate an ID. */
    MVMuint32 ID = tc->instance->num_reprs;
    tc->instance->num_reprs++;
    
    /* Stash ID and name. */
    repr->ID = ID;
    repr->name = name;
    
    /* Name should become a pernament GC root. */
    MVM_gc_root_add_pernament(tc, (MVMCollectable *)name);
    
    /* Enter into registry. */
    if (tc->instance->repr_registry)
        tc->instance->repr_registry = realloc(tc->instance->repr_registry,
            tc->instance->num_reprs * sizeof(MVMREPROps *));
    else
        tc->instance->repr_registry = malloc(tc->instance->num_reprs * sizeof(MVMREPROps *));
    tc->instance->repr_registry[ID] = repr;
    apr_hash_set(tc->instance->repr_name_to_id_hash,
        name->body.data, name->body.graphs * sizeof(MVMint32),
        &repr->ID);
        
    /* Add default "not implemented" function table implementations. */
    if (!repr->attr_funcs)
        add_default_attr_funcs(tc, repr);
    if (!repr->box_funcs)
        add_default_box_funcs(tc, repr);
    if (!repr->pos_funcs)
        add_default_pos_funcs(tc, repr);
    if (!repr->ass_funcs)
        add_default_ass_funcs(tc, repr);
}

/* Initializes the representations registry, building up all of the various
 * representations. */
void MVM_repr_initialize_registry(MVMThreadContext *tc) {
    /* Initialize name to ID map. */
    apr_pool_create(&tc->instance->repr_name_to_id_pool, NULL);
    tc->instance->repr_name_to_id_hash = apr_hash_make(tc->instance->repr_name_to_id_pool);
    
    /* Add all core representations. (If order changed, update reprs.h IDs.) */
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "MVMString"),
        MVMString_initialize(tc));
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "MVMArray"),
        MVMArray_initialize(tc));
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "MVMHash"),
        MVMHash_initialize(tc));
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "MVMCFunction"),
        MVMCFunction_initialize(tc));
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "KnowHOWREPR"),
        MVMKnowHOWREPR_initialize(tc));
    register_repr(tc,
        MVM_string_ascii_decode_nt(tc, tc->instance->boot_types->BOOTStr, "P6opaque"),
        MVMP6opaque_initialize(tc));
}

/* Get a representation's ID from its name. Note that the IDs may change so
 * it's best not to store references to them in e.g. the bytecode stream. */
MVMuint32 MVM_repr_name_to_id(MVMThreadContext *tc, MVMString *name) {
    MVMuint32 *value = (MVMuint32 *)apr_hash_get(tc->instance->repr_name_to_id_hash,
        name->body.data, name->body.graphs * sizeof(MVMint32));
    if (value == NULL)
        MVM_exception_throw_adhoc(tc, "Lookup by name of unknown REPR");
    return *value;
}

/* Gets a representation by ID. */
MVMREPROps * MVM_repr_get_by_id(MVMThreadContext *tc, MVMuint32 id) {
    return tc->instance->repr_registry[id];
}

/* Gets a representation by name. */
MVMREPROps * MVM_repr_get_by_name(MVMThreadContext *tc, MVMString *name) {
    return tc->instance->repr_registry[MVM_repr_name_to_id(tc, name)];
}
