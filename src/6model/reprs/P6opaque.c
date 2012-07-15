#include "moarvm.h"

/* This representation's function pointer table. */
static MVMREPROps *this_repr;

/* Helpers for reading/writing values. */
static MVMint64 get_int_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMint64 *)location);
}
static void set_int_at_offset(void *data, MVMint64 offset, MVMint64 value) {
    void *location = (char *)data + offset;
    *((MVMint64 *)location) = value;
}
static MVMnum64 get_num_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMnum64 *)location);
}
static void set_num_at_offset(void *data, MVMint64 offset, MVMnum64 value) {
    void *location = (char *)data + offset;
    *((MVMnum64 *)location) = value;
}
static MVMString * get_str_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMString **)location);
}
static void set_str_at_offset(MVMThreadContext *tc, MVMObject *root, void *data, MVMint64 offset, MVMString *value) {
    void *location = (char *)data + offset;
    MVM_ASSIGN_REF(tc, root, *((MVMString **)location), value);
}
static MVMObject * get_obj_at_offset(void *data, MVMint64 offset) {
    void *location = (char *)data + offset;
    return *((MVMObject **)location);
}
static void set_obj_at_offset(MVMThreadContext *tc, MVMObject *root, void *data, MVMint64 offset, MVMObject *value) {
    void *location = (char *)data + offset;
    MVM_ASSIGN_REF(tc, root, *((MVMObject **)location), value);
}

/* Helper for finding a slot number. */
static MVMint64 try_get_slot(MVMThreadContext *tc, MVMP6opaqueREPRData *repr_data, MVMObject *class_key, MVMString *name) {
    MVMint64 slot = -1;
    if (repr_data->name_to_index_mapping) {
        P6opaqueNameMap *cur_map_entry = repr_data->name_to_index_mapping;
        while (cur_map_entry->class_key != NULL) {
            if (cur_map_entry->class_key == class_key) {
                /* XXX Lookup */
            }
            cur_map_entry++;
        }
    }
    return slot;
}

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st;
    MVMObject *obj;
    
    st = MVM_gc_allocate_stable(tc, this_repr, HOW);
    MVM_gc_root_temp_push(tc, (MVMCollectable **)&st);
    
    obj = MVM_gc_allocate_type_object(tc, st);
    st->WHAT = obj;
    st->size = sizeof(MVMP6opaque); /* XXX Needs re-visiting later. */
    
    MVM_gc_root_temp_pop(tc);
    
    return st->WHAT;
}

/* Creates a new instance based on the type object. */
static MVMObject * allocate(MVMThreadContext *tc, MVMSTable *st) {
    /* XXX Work out allocation size, etc. */
    return MVM_gc_allocate_object(tc, st);
}

/* Initializes a new instance. */
static void initialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    /* XXX TODO */
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    /* XXX TODO */
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    /* XXX TODO */
}

/* Helper for complaining about attribute access errors. */
MVM_NO_RETURN
static void no_such_attribute(MVMThreadContext *tc, const char *action, MVMObject *class_handle, MVMString *name) {
    /* XXX awesomeize... */
    MVM_exception_throw_adhoc(tc, "P6opaque: not such attribute");
}

/* Gets the current value for an attribute. */
static MVMObject * get_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    
    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using get_attribute_boxed");

    /* Try the slot allocation first. */
    slot = hint >= 0 && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        if (!repr_data->flattened_stables[slot]) {
            MVMObject *result = get_obj_at_offset(data, repr_data->attribute_offsets[slot]);
            if (result) {
                return result;
            }
            else {
                /* Maybe we know how to auto-viv it to a container. */
                if (repr_data->auto_viv_values) {
                    MVMObject *value = repr_data->auto_viv_values[slot];
                    if (value != NULL) {
                        MVMObject *cloned = REPR(value)->allocate(tc, STABLE(value));
                        REPR(value)->copy_to(tc, STABLE(value), OBJECT_BODY(value), cloned, OBJECT_BODY(cloned));
                        set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], cloned);
                        return cloned;
                    }
                }
                return NULL;
            }
        }
        else {
            /* Need to produce a boxed version of this attribute. */
            MVMSTable *st = repr_data->flattened_stables[slot];
            MVMObject *result = st->REPR->allocate(tc, st);
            st->REPR->copy_to(tc, st, (char *)data + repr_data->attribute_offsets[slot],
                result, OBJECT_BODY(result));

            return result;
        }
    }
    
    /* Otherwise, complain that the attribute doesn't exist. */
    no_such_attribute(tc, "get", class_handle, name);
}

/* Binds the given value to the specified attribute. */
static void bind_attribute_boxed(MVMThreadContext *tc, MVMSTable *st, MVMObject *root,
        void *data, MVMObject *class_handle, MVMString *name, MVMint64 hint,
        MVMObject *value) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMint64 slot;
    
    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using bind_attribute_boxed");

    /* Try the slot allocation first. */
    slot = hint >= 0 && !(repr_data->mi) ? hint :
        try_get_slot(tc, repr_data, class_handle, name);
    if (slot >= 0) {
        MVMSTable *st = repr_data->flattened_stables[slot];
        if (st) {
            if (st == STABLE(value))
                st->REPR->copy_to(tc, st, OBJECT_BODY(value), root,
                    (char *)data + repr_data->attribute_offsets[slot]);
            else
                MVM_exception_throw_adhoc(tc,
                    "P6opaque: type mismatch when storing value to attribute");
        }
        else {
            set_obj_at_offset(tc, root, data, repr_data->attribute_offsets[slot], value);
        }
    }
    else {
        /* Otherwise, complain that the attribute doesn't exist. */
        no_such_attribute(tc, "bind", class_handle, name);
    }
}

/* Gets the hint for the given attribute ID. */
static MVMint64 hint_for(MVMThreadContext *tc, MVMSTable *st, MVMObject *class_key, MVMString *name) {
    MVMint64 slot;
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    if (!repr_data)
        MVM_exception_throw_adhoc(tc, "P6opaque: must compose before using hint_for");
    slot = try_get_slot(tc, repr_data, class_key, name);
    return slot >= 0 ? slot : MVM_NO_HINT;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMP6opaqueREPRData *repr_data = (MVMP6opaqueREPRData *)st->REPR_data;
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    if (repr_data) {
        if (repr_data->unbox_int_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_INT;
        if (repr_data->unbox_num_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_NUM;
        if (repr_data->unbox_str_slot >= 0)
            spec.can_box += MVM_STORAGE_SPEC_CAN_BOX_STR;
    }
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* XXX loads to do here! */
}

/* Initializes the representation. */
MVMREPROps * MVMP6opaque_initialize(MVMThreadContext *tc) {
    /* Allocate and populate the representation function table. */
    /* XXX Missing most things... */
    this_repr = malloc(sizeof(MVMREPROps));
    memset(this_repr, 0, sizeof(MVMREPROps));
    this_repr->type_object_for = type_object_for;
    this_repr->allocate = allocate;
    this_repr->initialize = initialize;
    this_repr->copy_to = copy_to;
    this_repr->gc_free = gc_free;
    this_repr->get_storage_spec = get_storage_spec;
    this_repr->compose = compose;
    this_repr->attr_funcs = malloc(sizeof(MVMREPROps_Attribute));
    memset(this_repr->attr_funcs, 0, sizeof(MVMREPROps_Attribute));
    this_repr->attr_funcs->get_attribute_boxed = get_attribute_boxed;
    /*this_repr->attr_funcs->get_attribute_ref = get_attribute_ref;*/
    this_repr->attr_funcs->bind_attribute_boxed = bind_attribute_boxed;
    /*this_repr->attr_funcs->bind_attribute_ref = bind_attribute_ref;*/
    /*this_repr->attr_funcs->is_attribute_initialized = is_attribute_initialized;*/
    this_repr->attr_funcs->hint_for = hint_for;
    return this_repr;
}
